#include "TextRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "TextLayoutBuilder.h"
#include "TextGlyphInstanceBuilder.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

using namespace Engine::TextLayoutBuilder;
using namespace Engine::TextGlyphInstanceBuilder;

//============================================================================
//	TextRenderBackend classMethods
//============================================================================
Engine::TextRenderBackend::~TextRenderBackend() {

	// FrameBatchResourcePool内のunique_ptrを終了時に明示resetする
	resourcePool_.Clear();
	vsGlyphScratch_.clear();
	psGlyphScratch_.clear();
}

namespace {

	// 描画に使用するフォントを描画アイテムのペイロードから解決する
	const Engine::MSDFFontAsset* ResolveFont(const Engine::RenderDrawContext& context, const Engine::RenderItem& item) {

		const auto* payload = context.batch->GetPayload<Engine::TextRenderPayload>(item);
		if (!payload || !payload->font) {
			return nullptr;
		}
		return context.assetLibrary->LoadFont(payload->font);
	}
}

void Engine::TextRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::TextRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// バッチ描画に使用するリソースを取得する
	TextBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](TextBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});

	// マテリアルパスを解決する
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!BackendDrawCommon::ResolveMaterialPass(context, items.front()->material,
		DefaultMaterialSlot::Text, { MaterialPassKind::Draw }, resolvedPass)) {
		return;
	}
	// 3Dテキストはメッシュと同じく前後遮蔽させたいので深度テスト+書き込みを有効にしたPSOを使う
	const bool is3D = items.front()->cameraDomain == RenderCameraDomain::Perspective;
	// パイプラインを解決する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass, nullptr, is3D);

	// 描画に使用するフォントを解決する
	const MSDFFontAsset* font = ResolveFont(context, *items.front());
	if (!font) {
		return;
	}
	// 描画に使用するテクスチャを解決する
	const GPUTextureResource* atlasTexture = BackendDrawCommon::ResolveTextureAsset(context, graphicsCore, font->atlasTexture);
	if (!atlasTexture) {
		return;
	}

	// GPUリソースの更新、2D/3Dで参照するカメラが異なるのでアイテムのドメインに合わせる
	resources.UpdateView(*context.view, items.front()->cameraDomain);

	// データクリア
	vsGlyphScratch_.clear();
	psGlyphScratch_.clear();

	for (const RenderItem* item : items) {

		const TextRenderPayload* payload = context.batch->GetPayload<TextRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		// コンポーネント本体を取得してランタイムキャッシュを使う
		if (!item->world) {
			continue;
		}

		auto* renderer = item->world->TryGetComponent<TextRendererComponent>(item->entity);
		if (!renderer) {
			continue;
		}

		// テキストやサイズが変わった時だけレイアウトを再構築する
		if (NeedsTextLayoutRebuild(*item->world, item->entity, *renderer, *font)) {
			if (!RebuildTextLayoutCache(
				*font, *item->world, item->entity, *renderer)) {
				continue;
			}
		}
		const TextLayoutRuntimeComponent* cache =
			item->world->TryGetComponent<TextLayoutRuntimeComponent>(item->entity);
		const std::span<const TextLayoutGlyph> glyphs =
			GetTextLayoutGlyphs(*item->world, item->entity);
		if (!cache || glyphs.empty()) {
			continue;
		}
		// キャッシュ済みレイアウトからVS/PSインスタンスだけ構築する
		Matrix4x4 worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, *context.view);
		if (item->cameraDomain == RenderCameraDomain::Perspective) {

			// グリフ座標はピクセル単位なのでワールド単位へ縮小し、3DはY+が上向きなので上下反転する
			const float s = renderer->worldScale;
			worldMatrix = Matrix4x4::MakeScaleMatrix(Vector3(s, -s, s)) * worldMatrix;
		}
		AppendGlyphInstancesFromCache(*renderer, *cache, glyphs,
			GetTextCharTransforms(*item->world, item->entity),
			worldMatrix, payload->uvMatrix,
			vsGlyphScratch_, psGlyphScratch_);
	}
	// 描画に使用するグリフがない場合は描画しない
	if (vsGlyphScratch_.empty() || psGlyphScratch_.empty()) {
		return;
	}
	// グリフインスタンスのデータをGPUにアップロードする
	resources.UploadGlyphs(vsGlyphScratch_, psGlyphScratch_);

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
		// テクスチャはDescriptorHandle経由でバインドする
		if (perDrawBindCache_.Has(atlasSRVSlot_) && atlasTexture->gpuHandle.ptr != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(atlasSRVSlot_),
				0, atlasTexture->gpuHandle);
		}
		// overrides持ちはバッチ分割で単独描画になるので先頭の上書きを使う
		if (resolvedPass.material) {
			const TextRenderPayload* firstPayload = context.batch->GetPayload<TextRenderPayload>(*items.front());
			BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, *pipelineState,
				*resolvedPass.material, firstPayload ? firstPayload->materialInstance : nullptr,
				perDrawBindCache_, materialParamsCBVSlot_, commandList);
		}
		// space2のマテリアルテクスチャをreflection駆動でバインドする、Builtinはspace2無で無回帰
		if (resolvedPass.material) {
			const TextRenderPayload* firstPayload = context.batch->GetPayload<TextRenderPayload>(*items.front());
			BackendDrawCommon::BindMaterialTextures(context, *pipelineState, materialParamBinder_,
				*resolvedPass.material, commandList,
				firstPayload ? firstPayload->materialInstance : nullptr);
		}
	}

	// 文字をインスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}

//============================================================================
//	TextRenderBackend classMethods
//============================================================================

namespace Engine {

	TextRenderBackend::TextRenderBackend() {

		vsInstSRVSlot_ = perDrawBindCache_.AddSlot("gVSInstances",  ShaderBindingKind::SRV);
		psInstSRVSlot_ = perDrawBindCache_.AddSlot("gPSInstances",  ShaderBindingKind::SRV);
		atlasSRVSlot_  = perDrawBindCache_.AddSlot("gAtlas",        ShaderBindingKind::SRV);
	}
}
