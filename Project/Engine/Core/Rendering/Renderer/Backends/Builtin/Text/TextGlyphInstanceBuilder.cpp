#include "TextGlyphInstanceBuilder.h"

namespace Engine::TextGlyphInstanceBuilder {

	void AppendGlyphInstancesFromCache(const Engine::TextRendererComponent& renderer,
		const Engine::TextLayoutRuntimeComponent& cache, std::span<const Engine::TextLayoutGlyph> glyphs,
		std::span<const Engine::TextCharTransform> charTransforms,
		const Engine::Matrix4x4& worldMatrix, const Engine::Matrix4x4& uvMatrix,
		std::vector<Engine::TextVSInstanceData>& outVS, std::vector<Engine::TextPSInstanceData>& outPS) {

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

				const Engine::Vector2 inverseBounds( cache.boundsSize.x > 0.0f ? 1.0f / cache.boundsSize.x : 0.0f,
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
