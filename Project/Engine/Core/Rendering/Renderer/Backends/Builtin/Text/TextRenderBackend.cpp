#include "TextRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

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
	bool NeedsTextLayoutRebuild(const Engine::ECSWorld& world,
		const Engine::Entity& entity,
		const Engine::TextRendererComponent& renderer,
		const Engine::MSDFFontAsset& font) {

		const auto* cache =
			world.TryGetComponent<Engine::TextLayoutRuntimeComponent>(entity);
		return !cache || !cache->valid || cache->font != renderer.font ||
			cache->fontContentRevision != font.contentRevision ||
			cache->textHash != Engine::HashTextLayoutString(renderer.text) ||
			cache->fontSize != renderer.fontSize ||
			cache->charSpacing != renderer.charSpacing;
	}

	// レイアウトだけをキャッシュする
	bool RebuildTextLayoutCache(const Engine::MSDFFontAsset& font,
		Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::TextRendererComponent& renderer) {

		auto* cache =
			world.TryGetComponent<Engine::TextLayoutRuntimeComponent>(entity);
		if (!cache) {
			return false;
		}
		Engine::DynamicBuffer<Engine::TextLayoutGlyph> glyphs =
			world.TryGetBuffer<Engine::TextLayoutGlyph>(entity);
		if (!glyphs.IsValid()) {
			return false;
		}

		// キャッシュをクリアして必要な情報を保存する
		cache->valid = false;
		cache->font = renderer.font;
		cache->fontContentRevision = font.contentRevision;
		cache->textHash = Engine::HashTextLayoutString(renderer.text);
		cache->fontSize = renderer.fontSize;
		cache->charSpacing = renderer.charSpacing;
		cache->atlasSize = font.GetAtlasSize();
		cache->pxRange = font.pxRange;
		cache->boundsSize = Engine::Vector2::AnyInit(0.0f);
		glyphs.Clear();

		// UTF-8 -> codepoint変換
		std::vector<char32_t> codepoints = Engine::Algorithm::Utf8ToCodepoints(renderer.text);
		if (codepoints.empty()) {
			cache->valid = true;
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

		glyphs.Reserve(static_cast<uint32_t>(codepoints.size()));

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

			glyphs.Add({
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

		if (glyphs.IsEmpty()) {
			cache->valid = true;
			return false;
		}

		const Engine::Vector2 origin = boundsMin;
		// ブロック全体のサイズを保存しておきインスタンス構築時のピボット基準にする
		cache->boundsSize = Engine::Vector2(
			boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y);
		for (Engine::TextLayoutGlyph& glyph : glyphs.GetSpan()) {
			glyph.rectMin -= origin;
			glyph.rectMax -= origin;
		}
		cache->valid = true;
		world.MarkComponentModified<Engine::TextLayoutGlyph>(entity);
		return true;
	}

	// キャッシュ済みレイアウトからVS/PSインスタンスを構築する
	void AppendGlyphInstancesFromCache(const Engine::TextRendererComponent& renderer,
		const Engine::TextLayoutRuntimeComponent& cache,
		std::span<const Engine::TextLayoutGlyph> glyphs,
		std::span<const Engine::TextCharTransform> charTransforms,
		const Engine::Matrix4x4& worldMatrix, const Engine::Matrix4x4& uvMatrix,
		std::vector<Engine::TextVSInstanceData>& outVS,
		std::vector<Engine::TextPSInstanceData>& outPS) {

		if (!cache.valid || glyphs.empty()) {
			return;
		}

		// 再確保回数を減らす
		outVS.reserve(outVS.size() + glyphs.size());
		outPS.reserve(outPS.size() + glyphs.size());

		for (size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {

			const Engine::TextLayoutGlyph& glyph = glyphs[glyphIndex];
			const Engine::TextCharTransform* charTransform =
				glyphIndex < charTransforms.size() ? &charTransforms[glyphIndex] : nullptr;
			const Engine::TextGlyphGeometry geometry = Engine::ResolveTextGlyphGeometry(
				renderer, cache, glyph, charTransform, worldMatrix);

			Engine::TextVSInstanceData vs{};
			vs.rectMin = geometry.rectMin;
			vs.rectMax = geometry.rectMax;
			vs.uvMin = glyph.uvMin;
			vs.uvMax = glyph.uvMax;
			if (renderer.uvPerCharacter) {
				vs.materialUVMin = Engine::Vector2::AnyInit(0.0f);
				vs.materialUVMax = Engine::Vector2::AnyInit(1.0f);
			} else {

				const Engine::Vector2 inverseBounds(
					cache.boundsSize.x > 0.0f ? 1.0f / cache.boundsSize.x : 0.0f,
					cache.boundsSize.y > 0.0f ? 1.0f / cache.boundsSize.y : 0.0f);
				vs.materialUVMin = Engine::Vector2(glyph.rectMin.x * inverseBounds.x, glyph.rectMin.y * inverseBounds.y);
				vs.materialUVMax = Engine::Vector2(glyph.rectMax.x * inverseBounds.x, glyph.rectMax.y * inverseBounds.y);
			}
			vs.worldMatrix = geometry.worldMatrix;
			outVS.emplace_back(vs);

			Engine::TextPSInstanceData ps{};
			ps.atlasSize = cache.atlasSize;
			ps.pxRange = cache.pxRange;
			ps.uvMatrix = uvMatrix;
			outPS.emplace_back(ps);
		}
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
