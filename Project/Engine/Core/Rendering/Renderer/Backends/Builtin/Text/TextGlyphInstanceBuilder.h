#pragma once

//============================================================================
//	include
//============================================================================
#include "TextBatchResources.h"
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

namespace Engine::TextGlyphInstanceBuilder {

	// 配置済み文字から描画instanceを作成
	void AppendGlyphInstancesFromCache(const TextRendererComponent& renderer, const TextLayoutRuntimeComponent& cache,
		std::span<const TextLayoutGlyph> glyphs, std::span<const TextCharTransform> charTransforms,
		const Matrix4x4& worldMatrix, const Matrix4x4& uvMatrix, std::vector<TextVSInstanceData>& outVS,
		std::vector<TextPSInstanceData>& outPS);
}
