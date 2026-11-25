#include "ShaderGraphAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Editor/UI/ImGui/EditorPalette.h>

// c++
#include <algorithm>
#include <utility>
// imgui
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace {

	constexpr float kDefaultNodeMinimumWidth = 180.0f;
	constexpr float kDefaultNodePreviewDisplaySize = 144.0f;
	constexpr int32_t kDefaultNodePreviewTextureSize = 128;

	// 四成分の保存値か既定値を取得する
	Engine::Vector4 ReadVector4(const nlohmann::json& data, const char* name, const Engine::Vector4& fallback) {

		const auto found = data.find(name);
		if (found == data.end() || !found->is_object()) {
			return fallback;
		}
		return Engine::Vector4(found->value("x", fallback.x), found->value("y", fallback.y), found->value("z", fallback.z),
			found->value("w", fallback.w));
	}

	// 二成分の保存値か既定値を取得する
	Engine::Vector2 ReadVector2(const nlohmann::json& data, const char* name, const Engine::Vector2& fallback) {

		const auto found = data.find(name);
		if (found == data.end() || !found->is_object()) {
			return fallback;
		}
		return Engine::Vector2(found->value("x", fallback.x), found->value("y", fallback.y));
	}
}

bool Engine::ShaderGraphAppearance::LoadAppearanceSettings(ShaderGraphAppearanceSetting& outSettings) try {

	nlohmann::json data;
	if (!JsonAdapter::TryLoad(RuntimePaths::GetEngineAssetPath(ConfigPaths::kShaderGraphAppearance), data) ||
		!data.is_object()) {
		return false;
	}

	// 既定値へ保存済み設定を重ねる
	ShaderGraphAppearanceSetting settings;
	RestoreDefaultAppearance(settings);
	const auto layoutFound = data.find("layout");
	if (layoutFound != data.end() && layoutFound->is_object()) {

		const nlohmann::json& layout = *layoutFound;
		settings.nodePadding = ReadVector4(layout, "nodePadding", settings.nodePadding);
		settings.linkStartOffset = ReadVector2(layout, "linkStartOffset", settings.linkStartOffset);
		settings.linkEndOffset = ReadVector2(layout, "linkEndOffset", settings.linkEndOffset);
		settings.nodeMinimumWidth = layout.value("nodeMinimumWidth", settings.nodeMinimumWidth);
		settings.nodePreviewDisplaySize = layout.value("nodePreviewDisplaySize", settings.nodePreviewDisplaySize);
		settings.nodePreviewTextureSize = layout.value("nodePreviewTextureSize", settings.nodePreviewTextureSize);
		settings.nodeRounding = layout.value("nodeRounding", settings.nodeRounding);
		settings.nodeBorderWidth = layout.value("nodeBorderWidth", settings.nodeBorderWidth);
		settings.hoveredNodeBorderWidth = layout.value("hoveredNodeBorderWidth", settings.hoveredNodeBorderWidth);
		settings.selectedNodeBorderWidth = layout.value("selectedNodeBorderWidth", settings.selectedNodeBorderWidth);
		settings.nodeTextScale = layout.value("nodeTextScale", settings.nodeTextScale);
		settings.gridSpacing = layout.value("gridSpacing", settings.gridSpacing);
		settings.nodeSnapGridSize = layout.value("nodeSnapGridSize", settings.nodeSnapGridSize);
		settings.pinRounding = layout.value("pinRounding", settings.pinRounding);
		settings.pinBorderWidth = layout.value("pinBorderWidth", settings.pinBorderWidth);
		settings.linkStrength = layout.value("linkStrength", settings.linkStrength);
		settings.linkThickness = layout.value("linkThickness", settings.linkThickness);
	}
	ClampAppearanceSettings(settings);
	outSettings = settings;
	return true;
} catch (const nlohmann::json::exception&) {
	return false;
}

bool Engine::ShaderGraphAppearance::SaveAppearanceSettings(const ShaderGraphAppearanceSetting& settings) {

	if (!EditorSourceNavigation::IsEngineSourceProject()) {
		return false;
	}

	nlohmann::json layout{
		{ "nodePadding", settings.nodePadding.ToJson() },
		{ "linkStartOffset", settings.linkStartOffset.ToJson() },
		{ "linkEndOffset", settings.linkEndOffset.ToJson() },
		{ "nodeMinimumWidth", settings.nodeMinimumWidth },
		{ "nodePreviewDisplaySize", settings.nodePreviewDisplaySize },
		{ "nodePreviewTextureSize", settings.nodePreviewTextureSize },
		{ "nodeRounding", settings.nodeRounding },
		{ "nodeBorderWidth", settings.nodeBorderWidth },
		{ "hoveredNodeBorderWidth", settings.hoveredNodeBorderWidth },
		{ "selectedNodeBorderWidth", settings.selectedNodeBorderWidth },
		{ "nodeTextScale", settings.nodeTextScale },
		{ "gridSpacing", settings.gridSpacing },
		{ "nodeSnapGridSize", settings.nodeSnapGridSize },
		{ "pinRounding", settings.pinRounding },
		{ "pinBorderWidth", settings.pinBorderWidth },
		{ "linkStrength", settings.linkStrength },
		{ "linkThickness", settings.linkThickness },
	};
	const nlohmann::json data{
		{ "schemaVersion", 5 },
		{ "layout", std::move(layout) },
	};
	return JsonAdapter::Save(RuntimePaths::GetEngineAssetPath(ConfigPaths::kShaderGraphAppearance), data);
}

void Engine::ShaderGraphAppearance::ApplyAppearanceSettings(const ShaderGraphAppearanceSetting& settings) {

	// Node Editorの描画状態へ設定を反映する
	ed::Style& style = ed::GetStyle();
	style.Colors[ed::StyleColor_Bg] = EditorPalette::Get("Graph.canvasBackground");
	style.Colors[ed::StyleColor_Grid] = EditorPalette::Get("Graph.grid");
	style.Colors[ed::StyleColor_NodeBg] = EditorPalette::Get("Graph.nodeBackground");
	style.Colors[ed::StyleColor_NodeBorder] = EditorPalette::Get("Graph.nodeBorder");
	style.Colors[ed::StyleColor_HovNodeBorder] = EditorPalette::Get("Graph.hoveredNodeBorder");
	style.Colors[ed::StyleColor_SelNodeBorder] = EditorPalette::Get("Graph.selectedNodeBorder");
	style.Colors[ed::StyleColor_NodeSelRect] = EditorPalette::Get("Graph.nodeSelection");
	style.Colors[ed::StyleColor_NodeSelRectBorder] = EditorPalette::Get("Graph.nodeSelectionBorder");
	style.Colors[ed::StyleColor_HovLinkBorder] = EditorPalette::Get("Graph.hoveredLinkBorder");
	style.Colors[ed::StyleColor_SelLinkBorder] = EditorPalette::Get("Graph.selectedLinkBorder");
	style.Colors[ed::StyleColor_HighlightLinkBorder] = EditorPalette::Get("Graph.highlightedLinkBorder");
	style.Colors[ed::StyleColor_LinkSelRect] = EditorPalette::Get("Graph.linkSelection");
	style.Colors[ed::StyleColor_LinkSelRectBorder] = EditorPalette::Get("Graph.linkSelectionBorder");
	style.Colors[ed::StyleColor_PinRect] = EditorPalette::Get("Graph.pinSelection");
	style.Colors[ed::StyleColor_PinRectBorder] = EditorPalette::Get("Graph.pinSelectionBorder");

	style.Colors[ed::StyleColor_Flow] = EditorPalette::Get("Graph.flow");
	style.Colors[ed::StyleColor_FlowMarker] = EditorPalette::Get("Graph.flowMarker");
	style.Colors[ed::StyleColor_GroupBg] = EditorPalette::Get("Graph.groupBackground");
	style.Colors[ed::StyleColor_GroupBorder] = EditorPalette::Get("Graph.groupBorder");

	style.NodePadding = ImVec4(settings.nodePadding.x, settings.nodePadding.y, settings.nodePadding.z,
		settings.nodePadding.w);
	style.NodeRounding = settings.nodeRounding;
	style.NodeBorderWidth = settings.nodeBorderWidth;
	style.HoveredNodeBorderWidth = settings.hoveredNodeBorderWidth;
	style.SelectedNodeBorderWidth = settings.selectedNodeBorderWidth;
	style.PinRounding = settings.pinRounding;
	style.PinBorderWidth = settings.pinBorderWidth;
	style.LinkStrength = settings.linkStrength;
	style.GridSpacing = settings.gridSpacing;
	style.NodeSnapGridSize = settings.nodeSnapGridSize;
}

void Engine::ShaderGraphAppearance::RestoreDefaultAppearance(ShaderGraphAppearanceSetting& settings) {

	// Node Editorの既定値から表示設定を作る
	const ed::Style style{};
	settings.nodePadding = Vector4(style.NodePadding.x, style.NodePadding.y, style.NodePadding.z, style.NodePadding.w);
	settings.linkStartOffset = Vector2{};
	settings.linkEndOffset = Vector2{};
	settings.nodeMinimumWidth = kDefaultNodeMinimumWidth;
	settings.nodePreviewDisplaySize = kDefaultNodePreviewDisplaySize;
	settings.nodePreviewTextureSize = kDefaultNodePreviewTextureSize;
	settings.nodeRounding = style.NodeRounding;
	settings.nodeBorderWidth = style.NodeBorderWidth;
	settings.hoveredNodeBorderWidth = style.HoveredNodeBorderWidth;
	settings.selectedNodeBorderWidth = style.SelectedNodeBorderWidth;
	settings.nodeTextScale = 1.0f;
	settings.gridSpacing = style.GridSpacing;
	settings.nodeSnapGridSize = style.NodeSnapGridSize;
	settings.pinRounding = style.PinRounding;
	settings.pinBorderWidth = style.PinBorderWidth;
	settings.linkStrength = style.LinkStrength;
	settings.linkThickness = 1.0f;
}

void Engine::ShaderGraphAppearance::ClampAppearanceSettings(ShaderGraphAppearanceSetting& settings) {

	// 表示欄と描画資源に使う値を範囲内へ収める
	settings.nodePadding.x = (std::clamp)(settings.nodePadding.x, 0.0f, 64.0f);
	settings.nodePadding.y = (std::clamp)(settings.nodePadding.y, 0.0f, 64.0f);
	settings.nodePadding.z = (std::clamp)(settings.nodePadding.z, 0.0f, 64.0f);
	settings.nodePadding.w = (std::clamp)(settings.nodePadding.w, 0.0f, 64.0f);
	settings.linkStartOffset.x = (std::clamp)(settings.linkStartOffset.x, -128.0f, 128.0f);
	settings.linkStartOffset.y = (std::clamp)(settings.linkStartOffset.y, -128.0f, 128.0f);
	settings.linkEndOffset.x = (std::clamp)(settings.linkEndOffset.x, -128.0f, 128.0f);
	settings.linkEndOffset.y = (std::clamp)(settings.linkEndOffset.y, -128.0f, 128.0f);
	settings.nodePreviewDisplaySize = (std::clamp)(settings.nodePreviewDisplaySize, 64.0f, 512.0f);
	settings.nodePreviewTextureSize = (std::clamp)(settings.nodePreviewTextureSize, 32, 1024);
	settings.nodeRounding = (std::clamp)(settings.nodeRounding, 0.0f, 32.0f);
	settings.nodeBorderWidth = (std::clamp)(settings.nodeBorderWidth, 0.0f, 10.0f);
	settings.hoveredNodeBorderWidth = (std::clamp)(settings.hoveredNodeBorderWidth, 0.0f, 10.0f);
	settings.selectedNodeBorderWidth = (std::clamp)(settings.selectedNodeBorderWidth, 0.0f, 10.0f);
	settings.nodeTextScale = (std::clamp)(settings.nodeTextScale, 0.5f, 2.0f);
	settings.gridSpacing = (std::clamp)(settings.gridSpacing, 4.0f, 256.0f);
	settings.nodeSnapGridSize = (std::clamp)(settings.nodeSnapGridSize, 0.0f, 256.0f);
	settings.pinRounding = (std::clamp)(settings.pinRounding, 0.0f, 16.0f);
	settings.pinBorderWidth = (std::clamp)(settings.pinBorderWidth, 0.0f, 10.0f);
	settings.linkStrength = (std::clamp)(settings.linkStrength, 0.0f, 500.0f);
	settings.linkThickness = (std::clamp)(settings.linkThickness, 0.1f, 10.0f);
}
