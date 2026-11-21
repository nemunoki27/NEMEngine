#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <string_view>
#include <imgui_internal.h>

namespace {

	std::vector<const Engine::ProjectDirectoryNode*> BuildBreadcrumbTrail(
		const Engine::ProjectAssetIndex& index, std::string_view path) {

		std::vector<const Engine::ProjectDirectoryNode*> trail{ &index.GetRoot() };
		while (trail.back()->virtualPath != path) {
			const auto children = trail.back()->GetChildren();
			const auto child = std::find_if(children.begin(), children.end(), [&](const auto& node) {
				const auto length = node.virtualPath.size();
				return path.starts_with(node.virtualPath) && (path.size() == length || path[length] == '/');
			});
			if (child == children.end()) break;
			trail.push_back(&*child);
		}
		return trail;
	}
}

//============================================================================
//	ProjectPanel classMethods
//============================================================================
void Engine::ProjectPanel::DrawBreadcrumb([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	// パンくずからの移動でも検索状態は解除する
	auto navigate = [&](const std::string& virtualPath) {
		selectedDirectory_ = virtualPath;
		selectedAsset_ = {};
		fileSearchFilter_.Clear();
	};

	const auto trail = BuildBreadcrumbTrail(assetIndex_, selectedDirectory_);
	const size_t last = trail.size() - 1;
	auto name = [&](size_t i) { return i == 0 ? GetSourceRootPath() : trail[i]->name.c_str(); };
	const auto& style = ImGui::GetStyle();
	const float gap = style.ItemSpacing.x * 2.0f + ImGui::CalcTextSize(">").x;
	auto buttonWidth = [&](const char* label) {
		return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f + gap;
	};
	const float available = ImGui::GetContentRegionAvail().x;
	float total = ImGui::CalcTextSize(name(last)).x;
	for (size_t i = 0; i < last; ++i) total += buttonWidth(name(i));

	size_t firstVisible = 1;
	bool showRoot = last != 0;
	if (last && total > available) {
		float used = buttonWidth("...") + ImGui::CalcTextSize(name(last)).x;
		showRoot = last > 1 && used + buttonWidth(name(0)) <= available;
		if (showRoot) used += buttonWidth(name(0));
		firstVisible = last;
		while (firstVisible > 1 && used + buttonWidth(name(firstVisible - 1)) <= available) {
			used += buttonWidth(name(--firstVisible));
		}
	}

	auto separator = [] {
		ImGui::SameLine();
		ImGui::TextDisabled(">");
		ImGui::SameLine();
	};
	auto drawFolder = [&](size_t i) {
		ImGui::PushID(trail[i]->virtualPath.c_str());
		if (ImGui::Button(name(i))) navigate(trail[i]->virtualPath);
		DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
		ImGui::PopID();
		separator();
	};
	if (showRoot) drawFolder(0);
	if (last && (!showRoot || firstVisible > 1)) {
		const bool clicked = ImGui::Button("...");
		const bool dragging = ImGui::GetDragDropPayload() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		if (clicked || dragging) ImGui::OpenPopup("##BreadcrumbFolders");
		if (ImGui::BeginPopup("##BreadcrumbFolders")) {
			for (size_t i = showRoot ? 1 : 0; i < firstVisible; ++i) {
				ImGui::PushID(trail[i]->virtualPath.c_str());
				if (ImGui::Selectable(name(i))) navigate(trail[i]->virtualPath);
				DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}
		separator();
	}
	for (size_t i = firstVisible; i < last; ++i) drawFolder(i);

	ImGui::AlignTextToFramePadding();
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
	const float baseline = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
	if (ImGui::InvisibleButton("##CurrentFolder", { width, ImGui::GetFrameHeight() })) navigate(trail[last]->virtualPath);
	ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), { start.x, start.y + baseline },
		{ start.x + width, start.y + ImGui::GetFrameHeight() }, start.x + width, name(last), nullptr, nullptr);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trail[last]->virtualPath.c_str());
	DrawProjectItemMoveDropTarget(database, trail[last]->virtualPath);
}
