#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>

namespace Engine::EditorShell {

	// OS既定の関連付けでファイルを開く、txtなど専用エディタを持たないアセット向け
	bool OpenWithSystemDefault(const std::filesystem::path& file);
	// 指定したディレクトリをエクスプローラーで開く
	bool OpenDirectory(const std::filesystem::path& directory);

} // Engine::EditorShell
