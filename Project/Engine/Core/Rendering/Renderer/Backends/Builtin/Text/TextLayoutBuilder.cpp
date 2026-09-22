#include "TextLayoutBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <limits>

namespace Engine::TextLayoutBuilder {

	void NormalizeMinMax(float& a, float& b) {
		if (b < a) {
			std::swap(a, b);
		}
	}

	bool NeedsTextLayoutRebuild(const Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::TextRendererComponent& renderer, const Engine::MSDFFontAsset& font) {

		const auto* cache =
			world.TryGetComponent<Engine::TextLayoutRuntimeComponent>(entity);
		return !cache || !cache->valid || cache->font != renderer.font ||
			cache->fontContentRevision != font.contentRevision ||
			cache->textHash != Engine::HashTextLayoutString(renderer.text) ||
			cache->fontSize != renderer.fontSize ||
			cache->charSpacing != renderer.charSpacing;
	}

	bool RebuildTextLayoutCache(const Engine::MSDFFontAsset& font,
		Engine::ECSWorld& world, const Engine::Entity& entity, Engine::TextRendererComponent& renderer) {

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
}
