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

	// computeカリングなのでバケットは参照しない、前提が欠ける条件を順に弾く
	(void)passBuckets;
	if (!context.shouldExecuteLightCullingPass || !context.resources || !context.assetDatabase ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}
	// ローカルライトが1つも無ければカリングする対象が無い
	if (!context.lightCullingBufferSet || context.lightCullingBufferSet->GetLocalLightCount() == 0) {
		return;
	}
	// GPU機能側でカリングOFFのときはPS側の全ライト評価へ任せる
	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (!runtimeFeatures.useLightCulling || runtimeFeatures.lightCullingMode == LightCullingMode::Disabled) {
		return;
	}

	// カリング用の専用リソースがあれば優先し、無ければ通常のresourcesから画面サイズを得る
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

	// LightCullingパスはcompute variant前提で、PSOが無ければ描画せず警告だけ出す
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

	// 画面サイズをthread group単位へ切り上げ、Clusteredのときだけ奥行方向にも分割する
	uint32_t dispatchX = DxUtils::RoundUp(sceneMain->GetWidth(), pipelineState->GetThreadGroupX());
	uint32_t dispatchY = DxUtils::RoundUp(sceneMain->GetHeight(), pipelineState->GetThreadGroupY());
	uint32_t dispatchZ = runtimeFeatures.lightCullingMode == LightCullingMode::Clustered ||
		runtimeFeatures.lightCullingMode == LightCullingMode::DebugAllLightsPerCluster ? ViewLightCullingBufferSet::kClusterCountZ : 1u;

	// UAV書き込みへ遷移してdispatchし、後続パスが読めるようSRV状態へ戻す
	context.lightCullingBufferSet->TransitionForComputeWrite(*dxCommand);
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);
	context.lightCullingBufferSet->TransitionForShaderRead(*dxCommand);
}
