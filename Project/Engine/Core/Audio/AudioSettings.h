#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace Engine {

	// Project単位の音声実行設定
	struct AudioSettings {
		bool playInBackground = true;

		// 無いファイルは既定値を使い、不正な設定は拒否する
		bool Load(const std::filesystem::path& path);
		// 保存結果を呼出元へ返す
		bool Save(const std::filesystem::path& path) const;
	};
}
