#include "TextGlyphInstanceBuilder.h"
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine::TextGlyphInstanceBuilder {

	void AppendGlyphInstancesFromCache(Entity entity, const Engine::TextRendererComponent& renderer,
		const Engine::TextLayoutRuntimeComponent& cache, std::span<const Engine::TextLayoutGlyph> glyphs,
		std::span<const Engine::TextCharTransform> charTransforms, std::span<const Engine::TextCharAnimationPose> animationPoses,
		const Engine::Matrix4x4& worldMatrix, const Engine::Matrix4x4& uvMatrix, const Engine::ShineHighlightData& shine,
		std::vector<Engine::TextVSInstanceData>& outVS, std::vector<Engine::TextPSInstanceData>& outPS) {

		if (!cache.valid || glyphs.empty()) {
			return;
		}

		// 再確保回数を減らす
		outVS.reserve(outVS.size() + glyphs.size());
		outPS.reserve(outPS.size() + glyphs.size());
		const Engine::Vector2 inverseBounds(
			cache.boundsSize.x > 0.0f ? 1.0f / cache.boundsSize.x : 0.0f,
			cache.boundsSize.y > 0.0f ? 1.0f / cache.boundsSize.y : 0.0f);

		for (size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {

			const Engine::TextLayoutGlyph& glyph = glyphs[glyphIndex];
			const Engine::TextCharTransform* charTransform =
				glyphIndex < charTransforms.size() ? &charTransforms[glyphIndex] : nullptr;
			const auto* animation = animationPoses.empty() ? nullptr : &animationPoses[glyphIndex];
			const Engine::TextGlyphGeometry geometry = Engine::ResolveTextGlyphGeometry(
				renderer, cache, glyph, charTransform, animation, worldMatrix);

			Engine::TextVSInstanceData vs{};
			vs.rectMin = geometry.rectMin;
			vs.rectMax = geometry.rectMax;
			vs.uvMin = glyph.uvMin;
			vs.uvMax = glyph.uvMax;
			vs.layoutUVMin = Engine::Vector2(glyph.rectMin.x * inverseBounds.x, glyph.rectMin.y * inverseBounds.y);
			vs.layoutUVMax = Engine::Vector2(glyph.rectMax.x * inverseBounds.x, glyph.rectMax.y * inverseBounds.y);
			if (!renderer.uvPerCharacter) {
				vs.materialUVMin = vs.layoutUVMin;
				vs.materialUVMax = vs.layoutUVMax;
			}
			vs.worldMatrix = geometry.worldMatrix;
			outVS.emplace_back(vs);

			Engine::TextPSInstanceData ps{};
			ps.atlasSize = cache.atlasSize;
			ps.pxRange = cache.pxRange;
			ps.uvMatrix = uvMatrix;
			ps.shine = shine;
			ps.entityIndex = entity.index;
			ps.entityGeneration = entity.generation;
			if (animation) ps.charColor = animation->color;
			outPS.emplace_back(ps);
		}
	}
}
