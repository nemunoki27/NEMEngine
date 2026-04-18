#include "TextRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Render/Backend/Common/BackendDrawCommon.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	TextRenderBackend classMethods
//============================================================================

namespace {

	// 2つの値を最小値がa、最大値がbになるように入れ替える
	void NormalizeMinMax(float& a, float& b) {
		if (b < a) {
			std::swap(a, b);
		}
	}
	// 描画に使用するフォントを描画アイテムのペイロードから解決する
	const Engine::MSDFFontAsset* ResolveFont(const Engine::RenderDrawContext& context, const Engine::RenderItem& item) {

		const auto* payload = context.batch->GetPayload<Engine::TextRenderPayload>(item);
		if (!payload || !payload->font) {
			return nullptr;
		}
		return context.assetLibrary->LoadFont(payload->font);
	}
	// 現在のレンダラー状態に対してレイアウトキャッシュを作り直す必要があるか
	bool NeedsTextLayoutRebuild(const Engine::TextRendererComponent& renderer) {

		const auto& cache = renderer.runtimeLayout;
		return !cache.valid || cache.font != renderer.font || cache.text != renderer.text ||
			cache.fontSize != renderer.fontSize || cache.charSpacing != renderer.charSpacing;
	}

	// // 追加:
	// レイアウトだけをキャッシュする
	bool RebuildTextLayoutCache(const Engine::MSDFFontAsset& font, Engine::TextRendererComponent& renderer) {

		auto& cache = renderer.runtimeLayout;

		// キャッシュをクリアして必要な情報を保存する
		cache.valid = false;
		cache.font = renderer.font;
		cache.text = renderer.text;
		cache.fontSize = renderer.fontSize;
		cache.charSpacing = renderer.charSpacing;
		cache.atlasSize = font.GetAtlasSize();
		cache.pxRange = font.pxRange;
		cache.glyphs.clear();

		// UTF-8 -> codepoint変換
		std::vector<char32_t> codepoints = Engine::Algorithm::Utf8ToCodepoints(renderer.text);
		if (codepoints.empty()) {
			return false;
		}

		float elementSize = font.metrics.elementSize;
		float lineHeight = font.metrics.lineHeight;
		// 0.0f以下の値の場合は再計算する
		if (lineHeight <= 0.0f) {

			lineHeight = font.metrics.ascender - font.metrics.descender;
			if (lineHeight <= 0.0f) {
				lineHeight = elementSize;
			}
		}

		float scale = renderer.fontSize / elementSize;
		float invAtlasW = 1.0f / static_cast<float>((std::max)(font.atlasWidth, 1u));
		float invAtlasH = 1.0f / static_cast<float>((std::max)(font.atlasHeight, 1u));

		cache.glyphs.reserve(codepoints.size());

		Engine::Vector2 boundsMin((std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)());
		Engine::Vector2 boundsMax(-(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)());

		float penX = 0.0f;
		float penY = 0.0f;
		char32_t prev = 0;
		for (char32_t cp : codepoints) {

			if (cp == U'\r') {
				continue;
			}
			if (cp == U'\n') {
				penX = 0.0f;
				penY += lineHeight * scale;
				prev = 0;
				continue;
			}

			const Engine::MSDFGlyph* glyph = font.FindGlyph(cp);
			if (!glyph) {
				continue;
			}

			if (prev != 0) {
				penX += font.GetKerning(prev, cp) * scale;
			}
			prev = cp;

			float advance = glyph->advance * scale;
			if (!glyph->planeBounds.has_value() || !glyph->atlasBounds.has_value()) {
				penX += advance + renderer.charSpacing;
				continue;
			}

			const auto& pb = glyph->planeBounds.value();
			const auto& ab = glyph->atlasBounds.value();

			float x0 = penX + pb.left * scale;
			float x1 = penX + pb.right * scale;
			float y0 = penY + pb.top * scale;
			float y1 = penY + pb.bottom * scale;

			NormalizeMinMax(x0, x1);
			NormalizeMinMax(y0, y1);

			float u0 = ab.left * invAtlasW;
			float u1 = ab.right * invAtlasW;
			float v0 = ab.top * invAtlasH;
			float v1 = ab.bottom * invAtlasH;

			NormalizeMinMax(u0, u1);
			NormalizeMinMax(v0, v1);

			cache.glyphs.push_back({
				.rectMin = Engine::Vector2(x0, y0),
				.rectMax = Engine::Vector2(x1, y1),
				.uvMin = Engine::Vector2(u0, v0),
				.uvMax = Engine::Vector2(u1, v1),
				});

			boundsMin.x = (std::min)(boundsMin.x, x0);
			boundsMin.y = (std::min)(boundsMin.y, y0);
			boundsMax.x = (std::max)(boundsMax.x, x1);
			boundsMax.y = (std::max)(boundsMax.y, y1);

			penX += advance + renderer.charSpacing;
		}

		if (cache.glyphs.empty()) {
			return false;
		}

		const Engine::Vector2 origin = boundsMin;
		for (auto& glyph : cache.glyphs) {
			glyph.rectMin -= origin;
			glyph.rectMax -= origin;
		}
		cache.valid = true;
		return true;
	}

	// キャッシュ済みレイアウトからVS/PSインスタンスを構築する
	void AppendGlyphInstancesFromCache(const Engine::TextRendererComponent& renderer,
		const Engine::Color& color, const Engine::Matrix4x4& worldMatrix,
		std::vector<Engine::TextVSInstanceData>& outVS,
		std::vector<Engine::TextPSInstanceData>& outPS) {

		const auto& cache = renderer.runtimeLayout;
		if (!cache.valid || cache.glyphs.empty()) {
			return;
		}

		// 再確保回数を減らす
		outVS.reserve(outVS.size() + cache.glyphs.size());
		outPS.reserve(outPS.size() + cache.glyphs.size());

		for (const auto& glyph : cache.glyphs) {

			Engine::TextVSInstanceData vs{};
			vs.rectMin = glyph.rectMin;
			vs.rectMax = glyph.rectMax;
			vs.uvMin = glyph.uvMin;
			vs.uvMax = glyph.uvMax;
			vs.worldMatrix = worldMatrix;
			outVS.emplace_back(vs);

			Engine::TextPSInstanceData ps{};
			ps.color = color;
			ps.atlasSize = cache.atlasSize;
			ps.pxRange = cache.pxRange;
			outPS.emplace_back(ps);
		}
	}
}

void Engine::TextRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	resourcePool_.BeginFrame();
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
		DefaultMaterialSlot::Text, { "Draw", "Text" }, resolvedPass)) {
		return;
	}
	// パイプラインを解決する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);

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

	// GPUリソースの更新
	resources.UpdateView(*context.view);

	// データクリア
	vsGlyphScratch_.clear();
	psGlyphScratch_.clear();

	for (const RenderItem* item : items) {

		const TextRenderPayload* payload = context.batch->GetPayload<TextRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		// コンポーネント本体を取得してランタイムキャッシュを使う
		if (!item->world || !item->world->IsAlive(item->entity) ||
			!item->world->HasComponent<TextRendererComponent>(item->entity)) {
			continue;
		}

		auto& renderer = item->world->GetComponent<TextRendererComponent>(item->entity);

		// テキストやサイズが変わった時だけレイアウトを再構築する
		if (NeedsTextLayoutRebuild(renderer)) {
			if (!RebuildTextLayoutCache(*font, renderer)) {
				continue;
			}
		} else if (renderer.runtimeLayout.glyphs.empty()) {
			continue;
		}
		// キャッシュ済みレイアウトからVS/PSインスタンスだけ構築する
		AppendGlyphInstancesFromCache(renderer, payload->color, item->worldMatrix, vsGlyphScratch_, psGlyphScratch_);
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
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, resources.GetGlyphVSBindingName(),
			resources.GetInstanceVSGPUAddress(), {}, bindScratch_);
		// PS
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, resources.GetGlyphPSBindingName(),
			resources.GetInstancePSGPUAddress(), {}, bindScratch_);
		// テクスチャ
		BackendDrawCommon::AppendGraphicsSRV(*pipelineState, "gAtlas", 0, atlasTexture->gpuHandle, bindScratch_);

		// バインド
		GraphicsRootBinder binder{ *pipelineState };
		binder.Bind(commandList, bindScratch_);
	}

	// 文字をインスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}

bool Engine::TextRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}