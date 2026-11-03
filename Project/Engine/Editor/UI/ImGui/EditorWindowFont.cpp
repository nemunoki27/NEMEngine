#include "EditorWindowPresentation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cstdio>
#include <filesystem>

void Engine::EditorWindowPresentation::ConfigureFont(ImGuiIO& io) {

	const auto path = RuntimePaths::GetEngineAssetPath("Fonts/Editor/FontAwesome/FontAwesomeFreeSolid.otf");
	if (!std::filesystem::exists(path)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "タイトル用のアイコンフォントが見つかりません");
		return;
	}

	ImFontConfig config{};
	config.MergeMode = true;
	config.GlyphMinAdvanceX = 20.0f;
	config.GlyphOffset.y = 2.0f;
	std::snprintf(config.Name, sizeof(config.Name), "NEM editor icons");
	const auto utf8 = path.u8string();
	io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), 20.0f, &config);
}
