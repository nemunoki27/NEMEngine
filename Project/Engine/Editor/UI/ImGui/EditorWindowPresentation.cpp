#include "EditorWindowPresentation.h"

//============================================================================
//	include
//============================================================================
#include <imgui_internal.h>

namespace {

	constexpr float kHeaderFontSize = 14.0f;
	const ImVec2 kHeaderPadding(6.0f, 4.0f);
	constexpr ImU32 kHeaderTextColor = IM_COL32(200, 200, 200, 255);

	struct WindowIcon {

		std::string_view key;
		const char* glyph;
	};

	constexpr WindowIcon kIcons[] = {
		{ "SceneView", "\xef\x80\x89" },
		{ "GameView", "\xef\x84\x9b" },
		{ "Hierarchy", "\xef\x83\xa8" },
		{ "Inspector", "\xef\x87\x9e" },
		{ "Project", "\xef\x81\xbc" },
		{ "Console", "\xef\x84\xa0" },
		{ "Toolbar", "\xef\x81\x8b" },
		{ "engine.sceneViewCamera", "\xef\x80\xb0" },
		{ "engine.collision_manager", "\xef\x89\x87" },
		{ "engine.animation_clip", "\xef\x80\x88" },
		{ "engine.timeline", "\xee\x8a\x9c" },
		{ "engine.render_features", "\xef\x97\xbd" },
		{ "engine.material_editor", "\xef\x94\xbf" },
		{ "engine.shader_graph", "\xef\x95\x82" },
		{ "engine.tag_manager", "\xef\x80\xac" },
		{ "engine.performance_check", "\xef\x88\x81" },
		{ "engine.script_execution_order", "\xef\x83\x8b" },
		{ "engine.script_profiler", "\xef\x8b\x9b" },
		{ "engine.input_device", "\xef\x84\x9c" },
		{ "engine.script_build_diagnostics", "\xef\x86\x88" },
		{ "engine.script_exception_list", "\xef\x81\xb1" },
	};
}

//============================================================================
//	EditorWindowPresentation methods
//============================================================================
const char* Engine::EditorWindowPresentation::GetIcon(std::string_view key) {

	for (const auto& icon : kIcons) {
		if (icon.key == key) {
			return icon.glyph;
		}
	}
	return "\xef\x82\xad";
}

bool Engine::EditorWindowPresentation::HasIconFont() {

	const ImFont* font = ImGui::GetIO().FontDefault;
	if (!font) {
		return false;
	}
	for (const ImFontConfig* source : font->Sources) {
		if (std::string_view(source->Name) == "NEM editor icons") {
			return true;
		}
	}
	return false;
}

const char* Engine::EditorWindowPresentation::GetPlainTitle(const char* title) {

	const std::string_view text(title);
	if (text.size() < 4 || text[3] != ' ') {
		return title;
	}
	for (const auto& icon : kIcons) {
		if (text.starts_with(icon.glyph)) {
			return title + 4;
		}
	}
	return text.starts_with(GetIcon({})) ? title + 4 : title;
}

std::string Engine::EditorWindowPresentation::MakeWindowName(
	std::string_view displayName, std::string_view identity, std::string_view iconKey) {

	std::string name;
	if (HasIconFont()) {
		name += GetIcon(iconKey);
		name += " ";
	}
	name += displayName;
	name += "###";
	name += identity;
	return name;
}

Engine::EditorWindowPresentation::HeaderScope::HeaderScope() {

	ImGui::PushFont(nullptr, kHeaderFontSize);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kHeaderPadding);
	ImGui::PushStyleColor(ImGuiCol_Text, kHeaderTextColor);
}

Engine::EditorWindowPresentation::HeaderScope::~HeaderScope() {

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	ImGui::PopFont();
}

void Engine::EditorWindowPresentation::FrameStyle::NewFrame() {

	auto& style = ImGui::GetStyle();
	bodyFontSize_ = style.FontSizeBase > 0.0f ? style.FontSizeBase : ImGui::GetDefaultFont()->LegacySize;
	const ImVec2 bodyPadding = style.FramePadding;
	const ImVec4 bodyTextColor = style.Colors[ImGuiCol_Text];
	style.FontSizeBase = kHeaderFontSize;
	style.FramePadding = kHeaderPadding;
	style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kHeaderTextColor);
	ImGui::NewFrame();
	style.FramePadding = bodyPadding;
	style.Colors[ImGuiCol_Text] = bodyTextColor;
	ImGui::PushFont(nullptr, bodyFontSize_);
}

void Engine::EditorWindowPresentation::FrameStyle::Render() {

	ImGui::PopFont();
	ImGui::Render();
	ImGui::GetStyle().FontSizeBase = bodyFontSize_;
}

bool Engine::EditorWindowPresentation::Begin(const char* name, bool* open, ImGuiWindowFlags flags) {

	if (!(ImGui::GetCurrentContext()->NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize) &&
		!(flags & ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::SetNextWindowSize(ImVec2(600.0f, 450.0f), ImGuiCond_FirstUseEver);
	}
	const HeaderScope header;
	return ImGui::Begin(name, open, flags);
}

void Engine::EditorWindowPresentation::PrepareDockNodes() {

	ImGuiContext& context = *ImGui::GetCurrentContext();
	constexpr ImGuiDockNodeFlags hiddenFlags = ImGuiDockNodeFlags_HiddenTabBar | static_cast<int>(ImGuiDockNodeFlags_AutoHideTabBar);
	for (const auto& entry : context.DockContext.Nodes.Data) {
		auto* node = static_cast<ImGuiDockNode*>(entry.val_p);
		if (!node) {
			continue;
		}
		node->SetLocalFlags(node->LocalFlags & ~hiddenFlags);
		node->WantHiddenTabBarToggle = false;
		node->WantHiddenTabBarUpdate = false;
	}
}
