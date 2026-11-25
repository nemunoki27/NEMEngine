#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>

#include <filesystem>
#include <string_view>
#include <imgui.h>

namespace Engine::EditorPalette {

	// エンジンの既定配色とユーザーの変更を読み込む
	bool Load(const std::filesystem::path& catalogDirectory, const std::filesystem::path& userFile);
	ImVec4 Get(std::string_view key);
	ImU32 U32(std::string_view key);
	Color4 RGBA(std::string_view key);
	void Set(std::string_view key, ImVec4 color);
	unsigned long long Revision();
	bool SaveUser();
	bool SaveDefaults();

	void Apply(ImGuiStyle& style, bool platformWindows);
	void DrawSettings(bool& open);
}
