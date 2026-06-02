#include "LightCullingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/ViewLightCullingBufferSet.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	LightCullingPass classMethods
//============================================================================

Engine::AssetID Engine::LightCullingPass::ResolveLightCullingMaterial(AssetDatabase& database) const {

	if (materialSearched_) {
		return cachedMaterialID_;
	}
	materialSearched_ = true;

	(void)database;
	// ビルトインMaterialはパスではなく.meta GUIDで固定参照する
	cachedMaterialID_ = BuiltinAssets::Materials::LightCulling;
	return cachedMaterialID_;
}

void Engine::LightCullingPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
	if (!context.resources || !context.assetDatabase || !deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}
	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (!runtimeFeatures.useLightCulling) {
		return;
	}

	RenderPathResources* cullingResources = context.lightCullingResources ?
		context.lightCullingResources : context.resources;
	MultiRenderTarget* sceneMain = cullingResources->GetSceneMain();
	if (!sceneMain) {
		return;
	}
	DepthTexture2D* depth = sceneMain->GetDepthTexture();
	if (!depth) {
		return;
	}

	AssetID materialID = ResolveLightCullingMaterial(*context.assetDatabase);
	if (!materialID) {
		return;
	}

	const MaterialAsset* material = deps_.assetLibrary->LoadMaterial(materialID);
	if (!material) {
		return;
	}
	const MaterialPassBinding* passBinding = FindPass(*material, "LightCulling");
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		return;
	}
	const PipelineState* pipelineState = deps_.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps_.assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, "[LightCullingPass] pipeline is missing.");
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// 深度をシェーダーリード用に遷移
	depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// 深度SRVをt1にバインド
	depthSRVCache_.Sync(*pipelineState);
	if (depthSRVCache_.Has(depthSRVSlot_)) {
		RootBindingCommand::SetComputeSRV(commandList, depthSRVCache_.Get(depthSRVSlot_),
			0, depth->GetSRVGPUHandle());
	}
	// バッファレジストリからライト関連バッファを自動バインド
	computeAutoBindTable_.Sync(*pipelineState, context.bufferRegistry);
	computeAutoBindTable_.BindCompute(context.bufferRegistry, commandList);

	const uint32_t dispatchX = DxUtils::RoundUp(sceneMain->GetWidth(), pipelineState->GetThreadGroupX());
	const uint32_t dispatchY = DxUtils::RoundUp(sceneMain->GetHeight(), pipelineState->GetThreadGroupY());
	const uint32_t dispatchZ =
		(runtimeFeatures.lightCullingMode == LightCullingMode::Clustered ||
		 runtimeFeatures.lightCullingMode == LightCullingMode::DebugAllLightsPerCluster) ?
		ViewLightCullingBufferSet::kClusterCountZ : 1u;
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);
}
