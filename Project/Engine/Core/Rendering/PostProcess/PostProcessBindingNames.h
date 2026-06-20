#pragma once

namespace Engine::PostProcessBindingNames {

	//============================================================================
	//	PostProcessBindingNames
	//	PostProcessシェーダーで予約済みのリソースバインド名、実体はここに一本化する
	//============================================================================

	// 入力カラーで現在パスのsourceが自動で割り当てられる
	inline constexpr const char* kSourceColor = "gSourceColor";
	// 入力深度で現在パスのsource深度が自動で割り当てられる
	inline constexpr const char* kSourceDepth = "gSourceDepth";
	// 出力先カラーで現在パスのdestが自動で割り当てられる
	inline constexpr const char* kDestColor = "gDestColor";
} // Engine::PostProcessBindingNames
