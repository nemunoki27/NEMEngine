#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include "RuntimeRenderPreloader.h"
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/Views/GameViewCameraSnapshot.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/BuiltinLightExtractors.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_set>

using namespace Engine;

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

bool RenderPipelineRunner::RenderMeshPicking(GraphicsCore& graphicsCore,
	RenderViewKind kind, const Vector2& inputPixel,
	MultiRenderTarget& target, std::optional<Dimension> dimensionFilter) {

	if (!pickingState_.lastRenderRequest_.world || !pickingState_.lastRenderRequest_.assetDatabase ||
		!pickingState_.lastActiveScene_ || !target.IsValid()) {
		return false;
	}

	const ResolvedRenderView& view = GetResolvedView(kind);
	const ResolvedCameraView* camera =
		view.FindCamera(RenderCameraDomain::Perspective);
	if (!view.valid || !camera) {
		return false;
	}

	std::vector<const RenderItem*> items{};
	items.reserve(scenePreparation_.renderBatch_.GetItems().size());
	for (const RenderItem& item : scenePreparation_.renderBatch_.GetItems()) {

		if ((item.backendID != RenderBackendID::Mesh &&
			item.backendID != RenderBackendID::Primitive) ||
			item.sceneInstanceID != pickingState_.lastActiveScene_->instanceID ||
			item.cameraDomain != RenderCameraDomain::Perspective ||
			(item.visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		if (dimensionFilter) {
			const TransformComponent* transform =
				pickingState_.lastRenderRequest_.world->TryGetComponent<TransformComponent>(item.entity);
			if (!transform || transform->dimension != *dimensionFilter) {
				continue;
			}
		}
		items.emplace_back(&item);
	}
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = pickingState_.lastActiveScene_;
	context.view = &view;
	context.cullingView = &view;
	context.defaultSurface = &target;
	context.billboardView =
		(kind == RenderViewKind::Scene && gameViewState_.view.valid) ?
		&gameViewState_.view : &view;
	context.disableInlineRayTracing = true;
	context.forceVertexMeshVariant = true;
	// 通常SceneView描画で使ったGameViewカリング結果を再利用せず、クリック画素へ全対象を描く
	context.disableMeshCulling = true;
	context.world = pickingState_.lastRenderRequest_.world;
	context.systemContext = pickingState_.lastRenderRequest_.systemContext;
	context.assetDatabase = pickingState_.lastRenderRequest_.assetDatabase;

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap()
		});
	target.TransitionForRender(*dxCommand);
	target.Bind(*dxCommand);
	target.Clear(*dxCommand, {
		.clearColor = true,
		.clearColorValue = Color4::Black(),
		.clearDepth = true,
		.clearDepthValue = 1.0f,
		});

	// 元ビューを負のオフセットで1x1 RTへ写し、クリック画素だけをラスタライズする
	const float pixelX = std::floor(std::clamp(
		inputPixel.x, 0.0f, static_cast<float>(view.width - 1)));
	const float pixelY = std::floor(std::clamp(
		inputPixel.y, 0.0f, static_cast<float>(view.height - 1)));
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = -pixelX;
	viewport.TopLeftY = -pixelY;
	viewport.Width = static_cast<float>(view.width);
	viewport.Height = static_cast<float>(view.height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	D3D12_RECT scissor{ 0, 0, 1, 1 };

	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	batchDispatcher_.Dispatch(graphicsCore, context, scenePreparation_.renderBatch_,
		backendRegistry_, renderAssetLibrary_, pipelineStateCache_,
		materialResolver_, items, &target, nullptr,
		MaterialPassKind::EditorPicking, false);
	return true;
}
