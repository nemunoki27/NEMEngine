#include "SpriteRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
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
	BeginFrameCommon();
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

	const SpriteRenderPayload* firstPayload = context.batch->GetPayload<SpriteRenderPayload>(*items.front());

	// GPUリソースの更新
	resources.UpdateView(*context.view, items.front()->cameraDomain);
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
		// レジストリのオートバインドとスロット解決をまとめて行う
		SyncAndBindRegistry(*pipelineState, context, commandList);
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
		// overrides持ちはCanBatchで単独描画になるので先頭の上書きを使う
		if (resolvedPass.material) {
			BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, *pipelineState,
				*resolvedPass.material, firstPayload ? firstPayload->materialInstance : nullptr,
				perDrawBindCache_, materialParamsCBVSlot_, commandList);
		}
		// space2のマテリアルテクスチャをreflection駆動でバインドする
		if (resolvedPass.material) {
			BackendDrawCommon::BindMaterialTextures(context, *pipelineState, materialParamBinder_,
				*resolvedPass.material, commandList,
				firstPayload ? firstPayload->materialInstance : nullptr);
		}
	}

	// インスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}
