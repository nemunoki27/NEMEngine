#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureHierarchyEditing.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

// c++
#include <algorithm>
#include <cctype>
#include <functional>
#include <string_view>

// imgui
#include <imgui.h>
#include <imgui_internal.h>

namespace {

	using HierarchyItem = Engine::RenderFeatureHierarchyItem;
	using HierarchyItemType = Engine::RenderFeatureHierarchyItemType;

	constexpr const char* kDragDropPayload = "NEM_RENDER_FEATURE_ITEM";

	using Engine::RenderFeatureHierarchyEditing::CanGroupSelection;
	using Engine::RenderFeatureHierarchyEditing::DeleteItem;
	using Engine::RenderFeatureHierarchyEditing::FindFeaturePass;
	using Engine::RenderFeatureHierarchyEditing::FindItemLocation;
	using Engine::RenderFeatureHierarchyEditing::GroupSelection;
	using Engine::RenderFeatureHierarchyEditing::HierarchyItemLocation;
	using Engine::RenderFeatureHierarchyEditing::MoveItemToGroup;
	using Engine::RenderFeatureHierarchyEditing::UngroupPass;
	std::string MakeMaterialPassName(const Engine::MaterialAsset& material) {

		std::string name = material.name;
		constexpr std::string_view suffix = "Material";
		const auto equalsIgnoreCase = [](char left, char right) {
			return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
		};
		if (suffix.size() <= name.size() && std::equal(suffix.rbegin(), suffix.rend(), name.rbegin(), equalsIgnoreCase)) {

			name.erase(name.size() - suffix.size());
		}
		return name.empty() ? "Render Feature" : name;
	}

	bool IsSelected(const std::vector<Engine::UUID>& selected, Engine::UUID id) {

		return std::ranges::find(selected, id) != selected.end();
	}

}

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================
void Engine::RenderFeatureProfileTool::DrawPassList(const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile = RenderFeatureProfileService::GetInstance().GetProfile();
	NormalizeRenderFeatureHierarchy(profile);
	const auto addPass = [&](RenderFeaturePassType type, std::string_view label, AssetID material = {},
							 MaterialPassKind materialPass = MaterialPassKind::Invalid) {
		RenderFeaturePassSettings pass{};
		pass.id = UUID::New();
		pass.name = label;
		pass.type = type;
		pass.material = material;
		pass.materialPass =
			materialPass == MaterialPassKind::Invalid
				? (type == RenderFeaturePassType::Compute ? MaterialPassKind::PostProcess : MaterialPassKind::RayTracing)
				: materialPass;
		pass.outputs.emplace_back();
		selectedPass_ = pass.id;
		selectedGroup_ = {};
		selectedPasses_ = {pass.id};
		profile.hierarchy.emplace_back(RenderFeatureHierarchyItem{
			.type = RenderFeatureHierarchyItemType::Pass,
			.id = pass.id,
		});
		profile.passes.emplace_back(std::move(pass));
		editSession_.SetDirty();
	};
	const auto addMaterialPass = [&](AssetID sourceAsset, AssetType assetType = AssetType::Material,
									 std::string_view assetPath = {}) {
		const AssetID materialID = editSession_.ResolvePassMaterial(context, sourceAsset, assetType, assetPath);
		if (!context.panelContext || !context.panelContext->renderPipeline || !materialID) {

			if (materialID) {
				editSession_.SetStatusMessage("マテリアルを読み込めません", true);
			}
			return;
		}
		RenderAssetLibrary& assetLibrary = context.panelContext->renderPipeline->GetRenderAssetLibrary();
		assetLibrary.InvalidateMaterial(materialID);
		const MaterialAsset* material = assetLibrary.LoadMaterial(materialID);
		if (!material) {
			editSession_.SetStatusMessage("マテリアルを読み込めません", true);
			return;
		}

		const MaterialPassBinding* postProcess = FindPass(*material, MaterialPassKind::PostProcess);
		const MaterialPassBinding* rayTracing = FindPass(*material, MaterialPassKind::RayTracing);
		const bool validPostProcess = postProcess && postProcess->preferredVariant == PipelineVariantKind::Compute;
		const bool validRayTracing = rayTracing && rayTracing->preferredVariant == PipelineVariantKind::Raytracing;
		const std::string name = MakeMaterialPassName(*material);
		if (material->domain == MaterialDomain::RayTracing && validRayTracing) {

			addPass(RenderFeaturePassType::RayTracing, name, materialID, MaterialPassKind::RayTracing);
		} else if (validPostProcess) {

			addPass(RenderFeaturePassType::Compute, name, materialID, MaterialPassKind::PostProcess);
		} else if (validRayTracing) {

			addPass(RenderFeaturePassType::RayTracing, name, materialID, MaterialPassKind::RayTracing);
		} else {

			editSession_.SetStatusMessage("ComputeまたはRayTracingパスがありません", true);
			return;
		}
		editSession_.SetStatusMessage("マテリアルからパスを追加しました", false);
	};

	PendingAction pending{};
	std::function<void(std::vector<HierarchyItem>&)> drawItems = [&](std::vector<HierarchyItem>& items) {
		for (HierarchyItem& item : items) {
			ImGui::PushID(static_cast<int32_t>(item.id.value >> 32));
			ImGui::PushID(static_cast<int32_t>(item.id.value));
			if (item.type == HierarchyItemType::Pass) {
				RenderFeaturePassSettings* pass = FindFeaturePass(profile, item.id);
				if (!pass) {
					ImGui::PopID();
					ImGui::PopID();
					continue;
				}
				// Play中は保存値を変更せず実行時の設定を表示する
				RenderFeatureRuntimeOverrides& overrides = RenderFeatureRuntimeOverrides::GetInstance();
				bool enabled = context.IsPlaying() ? overrides.IsEnabled(pass->id, pass->enabled) : pass->enabled;
				if (ImGui::Checkbox("##Enabled", &enabled)) {

					if (context.IsPlaying()) {

						overrides.SetEnabled(pass->id, enabled);
					} else {

						pass->enabled = enabled;
						editSession_.SetDirty();
					}
				}
				ImGui::SameLine();
				const bool selected = IsSelected(selectedPasses_, item.id);
				if (ImGui::Selectable(pass->name.c_str(), selected, ImGuiSelectableFlags_SpanAvailWidth)) {

					if (ImGui::GetIO().KeyShift) {
						if (selected) {
							std::erase(selectedPasses_, item.id);
						} else {
							selectedPasses_.emplace_back(item.id);
						}
						selectedPass_ = selectedPasses_.empty() ? UUID{} : selectedPasses_.back();
					} else {
						selectedPass_ = item.id;
						selectedPasses_ = {item.id};
					}
					selectedGroup_ = {};
				}
				if (ImGui::BeginDragDropSource()) {
					const DragDropPayload payload{
						.type = HierarchyItemType::Pass,
						.id = item.id.value,
					};
					ImGui::SetDragDropPayload(kDragDropPayload, &payload, sizeof(payload));
					ImGui::TextUnformatted(pass->name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginPopupContextItem("PassContext")) {
					if (!IsSelected(selectedPasses_, item.id)) {
						selectedPass_ = item.id;
						selectedPasses_ = {item.id};
						selectedGroup_ = {};
					}
					const bool canGroup = CanGroupSelection(profile, selectedPasses_);
					if (ImGui::MenuItem("グループ化", nullptr, false, canGroup)) {

						pending.type = PendingActionType::GroupSelection;
					}
					HierarchyItemLocation location{};
					const bool grouped =
						FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, item.id, location) && location.parentGroup;
					if (ImGui::MenuItem("グループ化解除", nullptr, false, grouped)) {

						pending = {
							.type = PendingActionType::UngroupPass,
							.itemType = HierarchyItemType::Pass,
							.item = item.id,
						};
					}
					ImGui::Separator();
					if (ImGui::MenuItem("削除")) {
						pending = {
							.type = PendingActionType::DeleteItem,
							.itemType = HierarchyItemType::Pass,
							.item = item.id,
						};
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				ImGui::PopID();
				continue;
			}

			if (ImGui::Checkbox("##Enabled", &item.enabled)) {
				editSession_.SetDirty();
			}
			ImGui::SameLine();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (selectedGroup_ == item.id) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			const bool opened = ImGui::TreeNodeEx("##Group", flags, "%s", item.name.c_str());
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				selectedGroup_ = item.id;
				selectedPass_ = {};
				selectedPasses_.clear();
			}
			if (ImGui::BeginDragDropSource()) {
				const DragDropPayload payload{
					.type = HierarchyItemType::Group,
					.id = item.id.value,
				};
				ImGui::SetDragDropPayload(kDragDropPayload, &payload, sizeof(payload));
				ImGui::TextUnformatted(item.name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragDropPayload)) {

					if (payload->IsDelivery() && payload->DataSize == sizeof(DragDropPayload)) {

						const auto* dragged = static_cast<const DragDropPayload*>(payload->Data);
						pending = {
							.type = PendingActionType::MoveToGroup,
							.itemType = dragged->type,
							.item = UUID{dragged->id},
							.targetGroup = item.id,
						};
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("GroupContext")) {
				selectedGroup_ = item.id;
				selectedPass_ = {};
				selectedPasses_.clear();
				if (ImGui::MenuItem("削除")) {
					pending = {
						.type = PendingActionType::DeleteItem,
						.itemType = HierarchyItemType::Group,
						.item = item.id,
					};
				}
				ImGui::EndPopup();
			}
			if (opened) {
				drawItems(item.children);
				ImGui::TreePop();
			}
			ImGui::PopID();
			ImGui::PopID();
		}
	};
	drawItems(profile.hierarchy);

	if (ImGui::BeginPopupContextWindow(
			"RenderFeaturePassListContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

		if (ImGui::BeginMenu("追加")) {
			for (const auto [type, label] : {std::pair{RenderFeaturePassType::Compute, "Compute"},
					 std::pair{RenderFeaturePassType::RayTracing, "DispatchRays"}}) {

				if (ImGui::MenuItem(label)) {
					addPass(type, label);
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
	if (dragging && dragging->IsDataType(IEditorPanel::kProjectAssetDragDropPayloadType) &&
		dragging->DataSize == sizeof(EditorAssetDragDropPayload)) {

		const auto* asset = static_cast<const EditorAssetDragDropPayload*>(dragging->Data);
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (!asset->isDirectory && asset->assetID && editSession_.IsPassMaterialSource(asset->assetType, asset->assetPath) &&
			ImGui::BeginDragDropTargetCustom(window->InnerRect, window->GetID("##RenderFeatureAssetDropTarget"))) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {

				if (payload->IsDelivery() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

					const auto* dropped = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
					if (!dropped->isDirectory && editSession_.IsPassMaterialSource(dropped->assetType, dropped->assetPath)) {

						addMaterialPass(dropped->assetID, dropped->assetType, dropped->assetPath);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	switch (pending.type) {
	case PendingActionType::GroupSelection:
		selectedGroup_ = GroupSelection(profile, selectedPasses_);
		if (selectedGroup_) {
			selectedPass_ = {};
			selectedPasses_.clear();
			editSession_.SetDirty();
		}
		break;
	case PendingActionType::UngroupPass:
		if (UngroupPass(profile, pending.item)) {
			editSession_.SetDirty();
		}
		break;
	case PendingActionType::DeleteItem:
		if (DeleteItem(profile, pending.itemType, pending.item)) {
			ClearSelection();
			editSession_.SetDirty();
		}
		break;
	case PendingActionType::MoveToGroup:
		if (MoveItemToGroup(profile, pending.itemType, pending.item, pending.targetGroup)) {

			editSession_.SetDirty();
		}
		break;
	case PendingActionType::None:
		break;
	}
}
