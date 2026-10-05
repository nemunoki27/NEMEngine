#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureHierarchyEditing.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <imgui.h>

namespace {

	using HierarchyItem = Engine::RenderFeatureHierarchyItem;
	using HierarchyItemType = Engine::RenderFeatureHierarchyItemType;
	using Engine::RenderFeatureHierarchyEditing::CollectPassIDs;
	using Engine::RenderFeatureHierarchyEditing::DeleteItem;
	using Engine::RenderFeatureHierarchyEditing::FindItemLocation;
	using Engine::RenderFeatureHierarchyEditing::HierarchyItemLocation;

	bool DrawApplicationSettings(
		const Engine::EditorToolContext& context, Engine::RenderFeatureSelectionSettings& selection, bool drawAnchor) {

		bool changed = false;
		const Engine::RenderFeatureSelectionMode previousMode = selection.mode;
		changed |= Engine::MyGUI::EnumCombo("適用方式", selection.mode).valueChanged;
		if (selection.mode == Engine::RenderFeatureSelectionMode::Organization) {

			return changed;
		}
		if (previousMode == Engine::RenderFeatureSelectionMode::Organization) {

			selection.renderingLayerMask = 1u;
			selection.phaseMask = Engine::MakeRenderFeaturePhaseMask(Engine::RenderPhase::Transparent);
			selection.rendererMask = Engine::RenderFeatureRendererMask::All;
		}
		if (drawAnchor) {
			changed |= Engine::MyGUI::EnumCombo("実行位置", selection.anchor).valueChanged;
		}
		changed |= Engine::InspectorDrawerCommon::DrawLayerMaskField(
			*context.panelContext, "Rendering Layer", selection.renderingLayerMask)
					   .valueChanged;

		const auto drawPhase = [&](const char* label, Engine::RenderPhase phase) {
			const uint32_t bit = Engine::MakeRenderFeaturePhaseMask(phase);
			bool enabled = (selection.phaseMask & bit) != 0u;
			if (Engine::MyGUI::Checkbox(label, enabled)) {
				if (enabled) {
					selection.phaseMask |= bit;
				} else {
					selection.phaseMask &= ~bit;
				}
				changed = true;
			}
		};
		ImGui::BeginDisabled(selection.mode == Engine::RenderFeatureSelectionMode::IsolatedLayer);
		drawPhase("Opaque", Engine::RenderPhase::Opaque);
		ImGui::EndDisabled();
		drawPhase("Transparent", Engine::RenderPhase::Transparent);
		drawPhase("PostProcess UI", Engine::RenderPhase::PostProcessUI);

		const auto drawRenderer = [&](const char* label, uint32_t bit) {
			bool enabled = (selection.rendererMask & bit) != 0u;
			if (Engine::MyGUI::Checkbox(label, enabled)) {
				if (enabled) {
					selection.rendererMask |= bit;
				} else {
					selection.rendererMask &= ~bit;
				}
				changed = true;
			}
		};
		drawRenderer("Mesh", Engine::RenderFeatureRendererMask::Mesh);
		drawRenderer("Primitive", Engine::RenderFeatureRendererMask::Primitive);
		drawRenderer("Sprite", Engine::RenderFeatureRendererMask::Sprite);
		drawRenderer("Text", Engine::RenderFeatureRendererMask::Text);
		drawRenderer("Line", Engine::RenderFeatureRendererMask::Line);
		drawRenderer("Particle", Engine::RenderFeatureRendererMask::Particle);

		if (selection.mode == Engine::RenderFeatureSelectionMode::IsolatedLayer) {

			selection.phaseMask &= ~Engine::MakeRenderFeaturePhaseMask(Engine::RenderPhase::Opaque);
			changed |= Engine::MyGUI::DragInt("合成レイヤー", selection.sortingLayer).valueChanged;
			changed |= Engine::MyGUI::DragInt("合成順", selection.sortingOrder).valueChanged;
			changed |= Engine::MyGUI::EnumCombo("合成方式", selection.compositeMode).valueChanged;
		}
		return changed;
	}
}

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================
bool Engine::RenderFeatureProfileTool::DrawSelectedPassControls(RenderFeatureProfileAsset& profile) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, selectedPass_, location)) {

		ClearSelection();
		return false;
	}
	const float buttonWidth = ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
	ImGui::BeginDisabled(location.index == 0);
	if (ImGui::Button("上へ", ImVec2(buttonWidth, 0.0f))) {
		std::swap((*location.siblings)[location.index], (*location.siblings)[location.index - 1]);
		SynchronizeRenderFeaturePassOrder(profile);
		editSession_.SetDirty();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(location.index + 1 >= location.siblings->size());
	if (ImGui::Button("下へ", ImVec2(buttonWidth, 0.0f))) {
		std::swap((*location.siblings)[location.index], (*location.siblings)[location.index + 1]);
		SynchronizeRenderFeaturePassOrder(profile);
		editSession_.SetDirty();
	}
	ImGui::EndDisabled();
	if (ImGui::Button("パスを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		DeleteItem(profile, HierarchyItemType::Pass, selectedPass_);
		ClearSelection();
		editSession_.SetDirty();
		return false;
	}
	return true;
}

void Engine::RenderFeatureProfileTool::DrawSelectedGroupDetail(
	const EditorToolContext& context, RenderFeatureProfileAsset& profile) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group, selectedGroup_, location)) {

		ClearSelection();
		return;
	}
	HierarchyItem& group = (*location.siblings)[location.index];
	bool changed = false;
	changed |= MyGUI::InputText("名前", group.name).valueChanged;
	changed |= MyGUI::Checkbox("有効", group.enabled);
	RenderFeatureSelectionSettings& selection = group.selection;
	const RenderFeatureAnchor previousAnchor = selection.anchor;
	changed |= DrawApplicationSettings(context, selection, true);
	if (selection.mode != RenderFeatureSelectionMode::Organization && previousAnchor != selection.anchor) {

		std::unordered_set<uint64_t> passIDs{};
		CollectPassIDs(group, passIDs);
		for (RenderFeaturePassSettings& pass : profile.passes) {
			if (passIDs.contains(pass.id.value)) {
				pass.anchor = selection.anchor;
			}
		}
	}
	if (changed) {
		editSession_.SetDirty();
	}
}

bool Engine::RenderFeatureProfileTool::DrawSelectedPassApplicationSettings(
	const EditorToolContext& context, RenderFeatureProfileAsset& profile, RenderFeaturePassSettings& pass) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, pass.id, location)) {

		return false;
	}
	RenderFeatureSelectionSettings& selection = (*location.siblings)[location.index].selection;
	selection.anchor = pass.anchor;
	return DrawApplicationSettings(context, selection, false);
}
