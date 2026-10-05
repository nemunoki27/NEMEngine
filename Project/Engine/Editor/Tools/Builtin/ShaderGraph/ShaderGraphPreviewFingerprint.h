#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine::ShaderGraphNodePreviewUtility {

	// 値と接続からプレビューの更新を判定する
	uint64_t CalculatePreviewHash(const Engine::ShaderGraphAsset& graph);

	// 更新判定用のハッシュへ値を加える
	uint64_t CombinePreviewHash(uint64_t seed, uint64_t value);

	// 設定値の型と内容からハッシュを作る
	uint64_t HashPreviewValue(const Engine::MaterialParameterValue& parameter);
} // Engine::ShaderGraphNodePreviewUtility
