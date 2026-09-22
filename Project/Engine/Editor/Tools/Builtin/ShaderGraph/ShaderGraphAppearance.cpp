#include "ShaderGraphAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

#include <algorithm>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace {

	constexpr float kDefaultNodeMinimumWidth = 180.0f;
	constexpr float kDefaultNodePreviewDisplaySize = 144.0f;
	constexpr int32_t kDefaultNodePreviewTextureSize = 128;
	Engine::Color4 ReadColor(
		const nlohmann::json& data,
		const char* name,
		const Engine::Color4& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Color4(
			found->value("r", fallback.r),
			found->value("g", fallback.g),
			found->value("b", fallback.b),
			found->value("a", fallback.a));
	}

	Engine::Vector4 ReadVector4(
		const nlohmann::json& data,
		const char* name,
		const Engine::Vector4& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Vector4(
			found->value("x", fallback.x),
			found->value("y", fallback.y),
			found->value("z", fallback.z),
			found->value("w", fallback.w));
	}

	Engine::Vector2 ReadVector2(
		const nlohmann::json& data,
		const char* name,
		const Engine::Vector2& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Vector2(
			found->value("x", fallback.x),
			found->value("y", fallback.y));
	}
}

ImVec4 Engine::ShaderGraphAppearance::ToImVec4(const Engine::Color4& color) {

	return ImVec4(color.r, color.g, color.b, color.a);
}

Engine::Color4 Engine::ShaderGraphAppearance::ToColor4(const ImVec4& color) {

	return Engine::Color4(
		color.x, color.y, color.z, color.w);
}

bool Engine::ShaderGraphAppearance::LoadAppearanceSettings(ShaderGraphAppearanceSetting& settings) {

	const std::filesystem::path path =
		RuntimePaths::GetUserSettingsPath(
			ConfigPaths::kShaderGraphAppearance);
	if (!JsonAdapter::Check(path)) {
		return false;
	}

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return false;
	}

	RestoreDefaultAppearance(settings);
	const auto colorsFound = data.find("colors");
	if (colorsFound != data.end() &&
		colorsFound->is_object()) {

		const nlohmann::json& colors = *colorsFound;
		settings.canvasBackground =
			ReadColor(colors, "canvasBackground",
				settings.canvasBackground);
		settings.grid =
			ReadColor(colors, "grid",
				settings.grid);
		settings.nodeBackground =
			ReadColor(colors, "nodeBackground",
				settings.nodeBackground);
		settings.nodeBorder =
			ReadColor(colors, "nodeBorder",
				settings.nodeBorder);
		settings.hoveredNodeBorder =
			ReadColor(colors, "hoveredNodeBorder",
				settings.hoveredNodeBorder);
		settings.selectedNodeBorder =
			ReadColor(colors, "selectedNodeBorder",
				settings.selectedNodeBorder);
		settings.nodeSelection =
			ReadColor(colors, "nodeSelection",
				settings.nodeSelection);
		settings.nodeSelectionBorder =
			ReadColor(colors, "nodeSelectionBorder",
				settings.nodeSelectionBorder);
		settings.link =
			ReadColor(colors, "link",
				settings.link);
		settings.hoveredLinkBorder =
			ReadColor(colors, "hoveredLinkBorder",
				settings.hoveredLinkBorder);
		settings.selectedLinkBorder =
			ReadColor(colors, "selectedLinkBorder",
				settings.selectedLinkBorder);
		settings.highlightedLinkBorder =
			ReadColor(colors, "highlightedLinkBorder",
				settings.highlightedLinkBorder);
		settings.linkSelection =
			ReadColor(colors, "linkSelection",
				settings.linkSelection);
		settings.linkSelectionBorder =
			ReadColor(colors, "linkSelectionBorder",
				settings.linkSelectionBorder);
		settings.pinSelection =
			ReadColor(colors, "pinSelection",
				settings.pinSelection);
		settings.pinSelectionBorder =
			ReadColor(colors, "pinSelectionBorder",
				settings.pinSelectionBorder);
	}

	const auto layoutFound = data.find("layout");
	if (layoutFound != data.end() &&
		layoutFound->is_object()) {

		const nlohmann::json& layout = *layoutFound;
		settings.nodePadding =
			ReadVector4(layout, "nodePadding",
				settings.nodePadding);
		settings.linkStartOffset =
			ReadVector2(layout, "linkStartOffset",
				settings.linkStartOffset);
		settings.linkEndOffset =
			ReadVector2(layout, "linkEndOffset",
				settings.linkEndOffset);
		settings.nodeMinimumWidth =
			layout.value("nodeMinimumWidth",
				settings.nodeMinimumWidth);
		settings.nodePreviewDisplaySize =
			layout.value("nodePreviewDisplaySize",
				settings.nodePreviewDisplaySize);
		settings.nodePreviewTextureSize =
			layout.value("nodePreviewTextureSize",
				settings.nodePreviewTextureSize);
		settings.nodeRounding =
			layout.value("nodeRounding",
				settings.nodeRounding);
		settings.nodeBorderWidth =
			layout.value("nodeBorderWidth",
				settings.nodeBorderWidth);
		settings.hoveredNodeBorderWidth =
			layout.value("hoveredNodeBorderWidth",
				settings.hoveredNodeBorderWidth);
		settings.selectedNodeBorderWidth =
			layout.value("selectedNodeBorderWidth",
				settings.selectedNodeBorderWidth);
		settings.nodeTextScale =
			layout.value("nodeTextScale",
				settings.nodeTextScale);
		settings.gridSpacing =
			layout.value("gridSpacing",
				settings.gridSpacing);
		settings.nodeSnapGridSize =
			layout.value("nodeSnapGridSize",
				settings.nodeSnapGridSize);
		settings.pinRounding =
			layout.value("pinRounding",
				settings.pinRounding);
		settings.pinBorderWidth =
			layout.value("pinBorderWidth",
				settings.pinBorderWidth);
		settings.linkStrength =
			layout.value("linkStrength",
				settings.linkStrength);
		settings.linkThickness =
			layout.value("linkThickness",
				settings.linkThickness);
	}
	ClampAppearanceSettings(settings);
	return true;
}

void Engine::ShaderGraphAppearance::SaveAppearanceSettings(const ShaderGraphAppearanceSetting& settings) {

	nlohmann::json colors{
		{ "canvasBackground",
			settings.canvasBackground.ToJson() },
		{ "grid", settings.grid.ToJson() },
		{ "nodeBackground",
			settings.nodeBackground.ToJson() },
		{ "nodeBorder",
			settings.nodeBorder.ToJson() },
		{ "hoveredNodeBorder",
			settings.hoveredNodeBorder.ToJson() },
		{ "selectedNodeBorder",
			settings.selectedNodeBorder.ToJson() },
		{ "nodeSelection",
			settings.nodeSelection.ToJson() },
		{ "nodeSelectionBorder",
			settings.nodeSelectionBorder.ToJson() },
		{ "link", settings.link.ToJson() },
		{ "hoveredLinkBorder",
			settings.hoveredLinkBorder.ToJson() },
		{ "selectedLinkBorder",
			settings.selectedLinkBorder.ToJson() },
		{ "highlightedLinkBorder",
			settings.highlightedLinkBorder.ToJson() },
		{ "linkSelection",
			settings.linkSelection.ToJson() },
		{ "linkSelectionBorder",
			settings.linkSelectionBorder.ToJson() },
		{ "pinSelection",
			settings.pinSelection.ToJson() },
		{ "pinSelectionBorder",
			settings.pinSelectionBorder.ToJson() },
	};
	nlohmann::json layout{
		{ "nodePadding",
			settings.nodePadding.ToJson() },
		{ "linkStartOffset",
			settings.linkStartOffset.ToJson() },
		{ "linkEndOffset",
			settings.linkEndOffset.ToJson() },
		{ "nodeMinimumWidth",
			settings.nodeMinimumWidth },
		{ "nodePreviewDisplaySize",
			settings.nodePreviewDisplaySize },
		{ "nodePreviewTextureSize",
			settings.nodePreviewTextureSize },
		{ "nodeRounding",
			settings.nodeRounding },
		{ "nodeBorderWidth",
			settings.nodeBorderWidth },
		{ "hoveredNodeBorderWidth",
			settings.hoveredNodeBorderWidth },
		{ "selectedNodeBorderWidth",
			settings.selectedNodeBorderWidth },
		{ "nodeTextScale",
			settings.nodeTextScale },
		{ "gridSpacing",
			settings.gridSpacing },
		{ "nodeSnapGridSize",
			settings.nodeSnapGridSize },
		{ "pinRounding",
			settings.pinRounding },
		{ "pinBorderWidth",
			settings.pinBorderWidth },
		{ "linkStrength",
			settings.linkStrength },
		{ "linkThickness",
			settings.linkThickness },
	};
	const nlohmann::json data{
		{ "schemaVersion", 4 },
		{ "colors", std::move(colors) },
		{ "layout", std::move(layout) },
	};
	JsonAdapter::Save(
		RuntimePaths::GetUserSettingsPath(
			ConfigPaths::kShaderGraphAppearance),
		data);
}

void Engine::ShaderGraphAppearance::ApplyAppearanceSettings(const ShaderGraphAppearanceSetting& settings) {

	ed::Style& style = ed::GetStyle();
	style.Colors[ed::StyleColor_Bg] =
		ToImVec4(settings.canvasBackground);
	style.Colors[ed::StyleColor_Grid] =
		ToImVec4(settings.grid);
	style.Colors[ed::StyleColor_NodeBg] =
		ToImVec4(settings.nodeBackground);
	style.Colors[ed::StyleColor_NodeBorder] =
		ToImVec4(settings.nodeBorder);
	style.Colors[ed::StyleColor_HovNodeBorder] =
		ToImVec4(settings.hoveredNodeBorder);
	style.Colors[ed::StyleColor_SelNodeBorder] =
		ToImVec4(settings.selectedNodeBorder);
	style.Colors[ed::StyleColor_NodeSelRect] =
		ToImVec4(settings.nodeSelection);
	style.Colors[ed::StyleColor_NodeSelRectBorder] =
		ToImVec4(settings.nodeSelectionBorder);
	style.Colors[ed::StyleColor_HovLinkBorder] =
		ToImVec4(settings.hoveredLinkBorder);
	style.Colors[ed::StyleColor_SelLinkBorder] =
		ToImVec4(settings.selectedLinkBorder);
	style.Colors[ed::StyleColor_HighlightLinkBorder] =
		ToImVec4(settings.highlightedLinkBorder);
	style.Colors[ed::StyleColor_LinkSelRect] =
		ToImVec4(settings.linkSelection);
	style.Colors[ed::StyleColor_LinkSelRectBorder] =
		ToImVec4(settings.linkSelectionBorder);
	style.Colors[ed::StyleColor_PinRect] =
		ToImVec4(settings.pinSelection);
	style.Colors[ed::StyleColor_PinRectBorder] =
		ToImVec4(settings.pinSelectionBorder);

	style.NodePadding = ImVec4(
		settings.nodePadding.x,
		settings.nodePadding.y,
		settings.nodePadding.z,
		settings.nodePadding.w);
	style.NodeRounding =
		settings.nodeRounding;
	style.NodeBorderWidth =
		settings.nodeBorderWidth;
	style.HoveredNodeBorderWidth =
		settings.hoveredNodeBorderWidth;
	style.SelectedNodeBorderWidth =
		settings.selectedNodeBorderWidth;
	style.PinRounding =
		settings.pinRounding;
	style.PinBorderWidth =
		settings.pinBorderWidth;
	style.LinkStrength =
		settings.linkStrength;
	style.GridSpacing =
		settings.gridSpacing;
	style.NodeSnapGridSize =
		settings.nodeSnapGridSize;
}

void Engine::ShaderGraphAppearance::RestoreDefaultAppearance(ShaderGraphAppearanceSetting& settings) {

	const ed::Style style{};
	settings.canvasBackground =
		ToColor4(style.Colors[ed::StyleColor_Bg]);
	settings.grid =
		ToColor4(style.Colors[ed::StyleColor_Grid]);
	settings.nodeBackground =
		ToColor4(style.Colors[ed::StyleColor_NodeBg]);
	settings.nodeBorder =
		ToColor4(style.Colors[ed::StyleColor_NodeBorder]);
	settings.hoveredNodeBorder =
		ToColor4(style.Colors[ed::StyleColor_HovNodeBorder]);
	settings.selectedNodeBorder =
		ToColor4(style.Colors[ed::StyleColor_SelNodeBorder]);
	settings.nodeSelection =
		ToColor4(style.Colors[ed::StyleColor_NodeSelRect]);
	settings.nodeSelectionBorder =
		ToColor4(style.Colors[
			ed::StyleColor_NodeSelRectBorder]);
	settings.link = Color4::White();
	settings.hoveredLinkBorder =
		ToColor4(style.Colors[ed::StyleColor_HovLinkBorder]);
	settings.selectedLinkBorder =
		ToColor4(style.Colors[ed::StyleColor_SelLinkBorder]);
	settings.highlightedLinkBorder =
		ToColor4(style.Colors[
			ed::StyleColor_HighlightLinkBorder]);
	settings.linkSelection =
		ToColor4(style.Colors[ed::StyleColor_LinkSelRect]);
	settings.linkSelectionBorder =
		ToColor4(style.Colors[
			ed::StyleColor_LinkSelRectBorder]);
	settings.pinSelection =
		ToColor4(style.Colors[ed::StyleColor_PinRect]);
	settings.pinSelectionBorder =
		ToColor4(style.Colors[ed::StyleColor_PinRectBorder]);

	settings.nodePadding = Vector4(
		style.NodePadding.x, style.NodePadding.y,
		style.NodePadding.z, style.NodePadding.w);
	settings.linkStartOffset = Vector2{};
	settings.linkEndOffset = Vector2{};
	settings.nodeMinimumWidth =
		kDefaultNodeMinimumWidth;
	settings.nodePreviewDisplaySize =
		kDefaultNodePreviewDisplaySize;
	settings.nodePreviewTextureSize =
		kDefaultNodePreviewTextureSize;
	settings.nodeRounding =
		style.NodeRounding;
	settings.nodeBorderWidth =
		style.NodeBorderWidth;
	settings.hoveredNodeBorderWidth =
		style.HoveredNodeBorderWidth;
	settings.selectedNodeBorderWidth =
		style.SelectedNodeBorderWidth;
	settings.nodeTextScale = 1.0f;
	settings.gridSpacing =
		style.GridSpacing;
	settings.nodeSnapGridSize =
		style.NodeSnapGridSize;
	settings.pinRounding =
		style.PinRounding;
	settings.pinBorderWidth =
		style.PinBorderWidth;
	settings.linkStrength =
		style.LinkStrength;
	settings.linkThickness = 1.0f;
}

void Engine::ShaderGraphAppearance::ClampAppearanceSettings(ShaderGraphAppearanceSetting& settings) {

	settings.nodePadding.x =
		(std::clamp)(settings.nodePadding.x, 0.0f, 64.0f);
	settings.nodePadding.y =
		(std::clamp)(settings.nodePadding.y, 0.0f, 64.0f);
	settings.nodePadding.z =
		(std::clamp)(settings.nodePadding.z, 0.0f, 64.0f);
	settings.nodePadding.w =
		(std::clamp)(settings.nodePadding.w, 0.0f, 64.0f);
	settings.linkStartOffset.x =
		(std::clamp)(settings.linkStartOffset.x, -128.0f, 128.0f);
	settings.linkStartOffset.y =
		(std::clamp)(settings.linkStartOffset.y, -128.0f, 128.0f);
	settings.linkEndOffset.x =
		(std::clamp)(settings.linkEndOffset.x, -128.0f, 128.0f);
	settings.linkEndOffset.y =
		(std::clamp)(settings.linkEndOffset.y, -128.0f, 128.0f);
	settings.nodePreviewDisplaySize =
		(std::clamp)(
			settings.nodePreviewDisplaySize,
			64.0f, 512.0f);
	settings.nodePreviewTextureSize =
		(std::clamp)(
			settings.nodePreviewTextureSize,
			32, 1024);
	settings.nodeRounding =
		(std::clamp)(settings.nodeRounding, 0.0f, 32.0f);
	settings.nodeBorderWidth =
		(std::clamp)(settings.nodeBorderWidth, 0.0f, 10.0f);
	settings.hoveredNodeBorderWidth =
		(std::clamp)(settings.hoveredNodeBorderWidth, 0.0f, 10.0f);
	settings.selectedNodeBorderWidth =
		(std::clamp)(settings.selectedNodeBorderWidth, 0.0f, 10.0f);
	settings.nodeTextScale =
		(std::clamp)(settings.nodeTextScale, 0.5f, 2.0f);
	settings.gridSpacing =
		(std::clamp)(settings.gridSpacing, 4.0f, 256.0f);
	settings.nodeSnapGridSize =
		(std::clamp)(settings.nodeSnapGridSize, 0.0f, 256.0f);
	settings.pinRounding =
		(std::clamp)(settings.pinRounding, 0.0f, 16.0f);
	settings.pinBorderWidth =
		(std::clamp)(settings.pinBorderWidth, 0.0f, 10.0f);
	settings.linkStrength =
		(std::clamp)(settings.linkStrength, 0.0f, 500.0f);
	settings.linkThickness =
		(std::clamp)(settings.linkThickness, 0.1f, 10.0f);
}
