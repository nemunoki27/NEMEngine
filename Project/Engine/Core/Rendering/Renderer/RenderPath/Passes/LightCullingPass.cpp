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
	if (!context.shouldExecuteLightCullingPass || !context.resources || !context.assetDatabase ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}
	if (!context.lightCullingBufferSet || context.lightCullingBufferSet->GetLocalLightCount() == 0) {
		return;
	}
	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (!runtimeFeatures.useLightCulling || runtimeFeatures.lightCullingMode == LightCullingMode::Disabled) {
		return;
	}

	RenderPathResources* cullingResources = context.lightCullingResources ?
		context.lightCullingResources : context.resources;
	MultiRenderTarget* sceneMain = cullingResources->GetSceneMain();
	if (!sceneMain) {
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
	const MaterialPassBinding* passBinding = FindPass(*material, MaterialPassKind::LightCulling);
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

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// バッファレジストリからライト関連バッファを自動バインド
	computeAutoBindTable_.Sync(*pipelineState, context.bufferRegistry);
	computeAutoBindTable_.BindCompute(context.bufferRegistry, commandList);

	uint32_t dispatchX = DxUtils::RoundUp(sceneMain->GetWidth(), pipelineState->GetThreadGroupX());
	uint32_t dispatchY = DxUtils::RoundUp(sceneMain->GetHeight(), pipelineState->GetThreadGroupY());
	uint32_t dispatchZ = runtimeFeatures.lightCullingMode == LightCullingMode::Clustered ||
		runtimeFeatures.lightCullingMode == LightCullingMode::DebugAllLightsPerCluster ? ViewLightCullingBufferSet::kClusterCountZ : 1u;

	// ライトカリング実行
	context.lightCullingBufferSet->TransitionForComputeWrite(*dxCommand);
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);
	context.lightCullingBufferSet->TransitionForShaderRead(*dxCommand);
}
