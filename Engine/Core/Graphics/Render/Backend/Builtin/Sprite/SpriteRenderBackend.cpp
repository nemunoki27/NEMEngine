#include "SpriteRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Render/Backend/Common/BackendDrawCommon.h>

//============================================================================
//	SpriteRenderBackend classMethods
//============================================================================

void Engine::SpriteRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	// フレーム開始時にプールをリセットする
	resourcePool_.BeginFrame();
}

void Engine::SpriteRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// バッチ描画に使用するリソースを取得する
	SpriteBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](SpriteBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});

	// マテリアルパスを解決する
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!BackendDrawCommon::ResolveMaterialPass(context, items.front()->material,
		DefaultMaterialSlot::Sprite, { "Draw", "Sprite" }, resolvedPass)) {
		return;
	}
	// パイプラインを解決する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);

	// 描画に使用するテクスチャを解決する
	const SpriteRenderPayload* firstPayload = context.batch->GetPayload<SpriteRenderPayload>(*items.front());
	const AssetID requestedTexture = firstPayload ? firstPayload->texture : AssetID{};
	const GPUTextureResource* texture = BackendDrawCommon::ResolveTextureAsset(context, graphicsCore, requestedTexture);

	// GPUリソースの更新
	resources.UpdateView(*context.view);
	resources.UploadInstances(*context.batch, items);

	// パイプラインを設定
	ID3D12GraphicsCommandList* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, items.front()->blendMode);

	// IAステージ設定
	{
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &resources.GetVBV());
		commandList->IASetIndexBuffer(&resources.GetIBV());
	}
	// ルートパラメータをバインド
	{
		// バッファバインディングの準備
		BackendDrawCommon::PrepareGraphicsBindItemsScratch(
			*context.bufferRegistry,
			3, // ViewConstants, gVSInstances, gPSInstances
			1, // gTexture
			bindScratch_);
		BackendDrawCommon::AppendGraphicsBufferBindings(*context.bufferRegistry, *pipelineState, bindScratch_);
		// View
		BackendDrawCommon::AppendGraphicsCBV(*pipelineState, resources.GetViewBindingName(),
			resources.GetViewGPUAddress(), bindScratch_);
		// VS
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, resources.GetInstanceVSBindingName(),
			resources.GetInstanceVSGPUAddress(), {}, bindScratch_);
		// PS
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, resources.GetInstancePSBindingName(),
			resources.GetInstancePSGPUAddress(), {}, bindScratch_);
		// テクスチャ
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, "gTexture", 0, texture->gpuHandle, bindScratch_);

		// バインド
		GraphicsRootBinder binder{ *pipelineState };
		binder.Bind(commandList, bindScratch_);
	}

	// インスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}

bool Engine::SpriteRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}