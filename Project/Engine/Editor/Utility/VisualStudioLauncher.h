#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

//============================================================================
//	VisualStudioLauncher namespace
//	ソースファイルをVisual Studioで開く
//============================================================================
namespace Engine::VisualStudioLauncher {

	// 既存のVisual Studioへファイルを渡し、未起動なら起動する
	bool OpenFile(const std::filesystem::path& file);
}
