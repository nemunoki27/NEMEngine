#pragma once

namespace Engine::PostProcessBindingNames {

	//============================================================================
	//	PostProcessBindingNames
	//	PostProcessシェーダーで予約済みのリソースバインド名
	//============================================================================

	// 入力カラーで現在パスのsourceが自動で割り当てられる
	inline constexpr const char* kSourceColor = "gSourceColor";
	// 入力深度で現在パスのsource深度が自動で割り当てられる
	inline constexpr const char* kSourceDepth = "gSourceDepth";
	// 出力先カラーで現在パスのdestが自動で割り当てられる
	inline constexpr const char* kDestColor = "gDestColor";
	// 選択対象へ適用する前のPostProcess結果
	inline constexpr const char* kEffectColor = "gEffectColor";
	// GBufferに保存したMaterial/描画対象フラグ
	inline constexpr const char* kSourceFlags = "gSourceFlags";
} // Engine::PostProcessBindingNames
