#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

#include <imgui.h>

namespace Engine {

	// ノード表示の外観設定
	struct ShaderGraphAppearanceSetting {

		Color4 canvasBackground{};
		Color4 grid{};
		Color4 nodeBackground{};
		Color4 nodeBorder{};
		Color4 hoveredNodeBorder{};
		Color4 selectedNodeBorder{};
		Color4 nodeSelection{};
		Color4 nodeSelectionBorder{};
		Color4 link{};
		Color4 hoveredLinkBorder{};
		Color4 selectedLinkBorder{};
		Color4 highlightedLinkBorder{};
		Color4 linkSelection{};
		Color4 linkSelectionBorder{};
		Color4 pinSelection{};
		Color4 pinSelectionBorder{};

		Vector4 nodePadding{};
		Vector2 linkStartOffset{};
		Vector2 linkEndOffset{};
		float nodeMinimumWidth = 180.0f;
		float nodePreviewDisplaySize = 144.0f;
		float nodeRounding = 0.0f;
		float nodeBorderWidth = 0.0f;
		float hoveredNodeBorderWidth = 0.0f;
		float selectedNodeBorderWidth = 0.0f;
		float nodeTextScale = 1.0f;
		float gridSpacing = 32.0f;
		float nodeSnapGridSize = 16.0f;
		float pinRounding = 0.0f;
		float pinBorderWidth = 0.0f;
		float linkStrength = 0.0f;
		float linkThickness = 0.0f;
		int32_t nodePreviewTextureSize = 128;
	};
}

namespace Engine::ShaderGraphAppearance {

	// 見た目設定を読み込む
	bool LoadAppearanceSettings(ShaderGraphAppearanceSetting& settings);
	// 見た目設定を保存する
	void SaveAppearanceSettings(const ShaderGraphAppearanceSetting& settings);
	// 見た目設定を描画へ適用する
	void ApplyAppearanceSettings(const ShaderGraphAppearanceSetting& settings);
	// 見た目設定を初期値へ戻す
	void RestoreDefaultAppearance(ShaderGraphAppearanceSetting& settings);
	// 見た目設定を有効範囲へ補正する
	void ClampAppearanceSettings(ShaderGraphAppearanceSetting& settings);
	// ImGuiの色表現を変換する
ImVec4 ToImVec4(const Engine::Color4& color);
	// ImGuiの色表現を変換する
Engine::Color4 ToColor4(const ImVec4& color);
}
