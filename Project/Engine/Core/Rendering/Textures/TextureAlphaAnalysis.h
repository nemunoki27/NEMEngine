#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <unordered_map>

namespace Engine {

	enum class TextureAlphaContent {

		Opaque,
		Masked,
		Transparent,
	};
	//============================================================================
	//	TextureAlphaAnalysis class
	//	画像の透明画素をモデル読込中に共有する
	//============================================================================
	class TextureAlphaAnalysis {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 基底画像のAlphaから表面方式を判定する
		TextureAlphaContent Analyze(const std::filesystem::path& path);
		// モデル変更時に解析結果を破棄する
		void Clear();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::filesystem::path, TextureAlphaContent> cache_;
	};
}
