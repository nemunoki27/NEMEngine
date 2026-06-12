#include "SpriteRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>

//============================================================================
//	SpriteRenderBackend classMethods
//============================================================================
Engine::SpriteRenderBackend::~SpriteRenderBackend() {

	// FrameBatchResourcePool内のunique_ptrを終了時に明示resetする
	resourcePool_.Clear();
}

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
		DefaultMaterialSlot::Sprite, { MaterialPassKind::Draw }, resolvedPass)) {
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
	resources.UploadInstances(*context.view, *context.batch, items);

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
		// バッファレジストリ登録済みバッファをまとめてバインドする
		registryAutoBindTable_.Sync(*pipelineState, *context.bufferRegistry);
		registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);

		// 描画固有バインドのスロット解決を更新
		perDrawBindCache_.Sync(*pipelineState);
		if (perDrawBindCache_.Has(viewCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_),
				resources.GetViewGPUAddress());
		}
		if (perDrawBindCache_.Has(vsInstSRVSlot_) && resources.GetInstanceVSGPUAddress() != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(vsInstSRVSlot_),
				resources.GetInstanceVSGPUAddress(), {});
		}
		if (perDrawBindCache_.Has(psInstSRVSlot_) && resources.GetInstancePSGPUAddress() != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(psInstSRVSlot_),
				resources.GetInstancePSGPUAddress(), {});
		}
		// テクスチャはDescriptorHandle経由でバインドする
		if (perDrawBindCache_.Has(textureSRVSlot_) && texture && texture->gpuHandle.ptr != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(textureSRVSlot_),
				0, texture->gpuHandle);
		}
	}

	// インスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}

bool Engine::SpriteRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}
