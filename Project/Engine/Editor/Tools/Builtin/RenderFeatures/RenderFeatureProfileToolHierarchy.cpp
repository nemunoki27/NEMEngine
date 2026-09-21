#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>
#include <cctype>
#include <functional>
#include <string_view>
#include <unordered_set>

#include <imgui.h>
#include <imgui_internal.h>

namespace {

	using HierarchyItem = Engine::RenderFeatureHierarchyItem;
	using HierarchyItemType = Engine::RenderFeatureHierarchyItemType;

	constexpr const char* kDragDropPayload = "NEM_RENDER_FEATURE_ITEM";

	struct HierarchyItemLocation {

		std::vector<HierarchyItem>* siblings = nullptr;
		size_t index = 0;
		HierarchyItem* parentGroup = nullptr;
	};

	struct DragDropPayload {

		HierarchyItemType type = HierarchyItemType::Pass;
		uint64_t id = 0;
	};

	enum class PendingActionType : uint8_t {

		None,
		GroupSelection,
		UngroupPass,
		DeleteItem,
		MoveToGroup,
	};

	struct PendingAction {

		PendingActionType type = PendingActionType::None;
		HierarchyItemType itemType = HierarchyItemType::Pass;
		Engine::UUID item{};
		Engine::UUID targetGroup{};
	};

	std::string MakeMaterialPassName(const Engine::MaterialAsset& material) {

		std::string name = material.name;
		constexpr std::string_view suffix = "Material";
		const auto equalsIgnoreCase = [](char left, char right) {

			return std::tolower(static_cast<unsigned char>(left)) ==
				std::tolower(static_cast<unsigned char>(right));
		};
		if (suffix.size() <= name.size() && std::equal(
			suffix.rbegin(), suffix.rend(), name.rbegin(), equalsIgnoreCase)) {

			name.erase(name.size() - suffix.size());
		}
		return name.empty() ? "Render Feature" : name;
	}

	bool IsSelected(const std::vector<Engine::UUID>& selected,
		Engine::UUID id) {

		return std::ranges::find(selected, id) != selected.end();
	}

	bool FindItemLocation(std::vector<HierarchyItem>& items,
		HierarchyItemType type, Engine::UUID id,
		HierarchyItemLocation& outLocation,
		HierarchyItem* parentGroup = nullptr) {

		for (size_t index = 0; index < items.size(); ++index) {
			HierarchyItem& item = items[index];
			if (item.type == type && item.id == id) {
				outLocation = {
					.siblings = &items,
					.index = index,
					.parentGroup = parentGroup,
				};
				return true;
			}
			if (item.type == HierarchyItemType::Group &&
				FindItemLocation(item.children, type, id,
					outLocation, &item)) {

				return true;
			}
		}
		return false;
	}

	const HierarchyItem* FindItem(
		const std::vector<HierarchyItem>& items,
		HierarchyItemType type, Engine::UUID id) {

		for (const HierarchyItem& item : items) {
			if (item.type == type && item.id == id) {
				return &item;
			}
			if (item.type == HierarchyItemType::Group) {
				if (const HierarchyItem* found = FindItem(
					item.children, type, id)) {

					return found;
				}
			}
		}
		return nullptr;
	}

	Engine::RenderFeaturePassSettings* FindFeaturePass(
		Engine::RenderFeatureProfileAsset& profile, Engine::UUID id) {

		const auto found = std::find_if(profile.passes.begin(),
			profile.passes.end(), [id](const auto& pass) {

				return pass.id == id;
			});
		return found == profile.passes.end() ? nullptr : &*found;
	}

	void CollectPassIDs(const HierarchyItem& item,
		std::unordered_set<uint64_t>& outPasses) {

		if (item.type == HierarchyItemType::Pass) {
			outPasses.emplace(item.id.value);
			return;
		}
		for (const HierarchyItem& child : item.children) {
			CollectPassIDs(child, outPasses);
		}
	}

	void ErasePasses(Engine::RenderFeatureProfileAsset& profile,
		const std::unordered_set<uint64_t>& passIDs) {

		std::erase_if(profile.passes, [&](const auto& pass) {

			return passIDs.contains(pass.id.value);
		});
		for (Engine::RenderFeaturePassSettings& pass : profile.passes) {
			if (pass.source.pass && passIDs.contains(pass.source.pass.value)) {
				pass.sourceKind = Engine::RenderFeatureSourceKind::PreviousPass;
				pass.source = {};
			}
			std::erase_if(pass.passInputs, [&](const auto& input) {

				return input.second.pass &&
					passIDs.contains(input.second.pass.value);
			});
		}
	}

	bool DeleteItem(Engine::RenderFeatureProfileAsset& profile,
		HierarchyItemType type, Engine::UUID id) {

		HierarchyItemLocation location{};
		if (!FindItemLocation(profile.hierarchy, type, id, location)) {
			return false;
		}
		std::unordered_set<uint64_t> passIDs{};
		CollectPassIDs((*location.siblings)[location.index], passIDs);
		location.siblings->erase(location.siblings->begin() + location.index);
		ErasePasses(profile, passIDs);
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

	bool CanGroupSelection(Engine::RenderFeatureProfileAsset& profile,
		const std::vector<Engine::UUID>& selectedPasses) {

		if (selectedPasses.size() < 2) {
			return false;
		}
		std::vector<size_t> indices{};
		std::vector<HierarchyItem>* siblings = nullptr;
		for (Engine::UUID passID : selectedPasses) {
			HierarchyItemLocation location{};
			if (!FindItemLocation(profile.hierarchy,
				HierarchyItemType::Pass, passID, location)) {

				return false;
			}
			if (siblings && siblings != location.siblings) {
				return false;
			}
			siblings = location.siblings;
			indices.emplace_back(location.index);
		}
		std::ranges::sort(indices);
		return std::adjacent_find(indices.begin(), indices.end(),
			[](size_t left, size_t right) {

				return right != left + 1;
			}) == indices.end();
	}

	bool HasGroupName(const std::vector<HierarchyItem>& items,
		std::string_view name) {

		for (const HierarchyItem& item : items) {
			if (item.type != HierarchyItemType::Group) {
				continue;
			}
			if (item.name == name || HasGroupName(item.children, name)) {
				return true;
			}
		}
		return false;
	}

	std::string MakeGroupName(
		const Engine::RenderFeatureProfileAsset& profile) {

		std::string name = "グループ";
		for (uint32_t suffix = 2; HasGroupName(profile.hierarchy, name);
			++suffix) {

			name = "グループ " + std::to_string(suffix);
		}
		return name;
	}

	Engine::UUID GroupSelection(Engine::RenderFeatureProfileAsset& profile,
		const std::vector<Engine::UUID>& selectedPasses) {

		if (!CanGroupSelection(profile, selectedPasses)) {
			return {};
		}
		std::vector<size_t> indices{};
		std::vector<HierarchyItem>* siblings = nullptr;
		for (Engine::UUID passID : selectedPasses) {
			HierarchyItemLocation location{};
			FindItemLocation(profile.hierarchy,
				HierarchyItemType::Pass, passID, location);
			siblings = location.siblings;
			indices.emplace_back(location.index);
		}
		std::ranges::sort(indices);
		const size_t firstIndex = indices.front();
		const size_t lastIndex = indices.back();

		HierarchyItem group{
			.type = HierarchyItemType::Group,
			.id = Engine::UUID::New(),
			.name = MakeGroupName(profile),
		};
		for (size_t index = firstIndex; index <= lastIndex; ++index) {
			group.children.emplace_back(std::move((*siblings)[index]));
		}
		siblings->erase(siblings->begin() + firstIndex,
			siblings->begin() + lastIndex + 1);
		const Engine::UUID groupID = group.id;
		siblings->insert(siblings->begin() + firstIndex, std::move(group));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return groupID;
	}

	bool UngroupPass(Engine::RenderFeatureProfileAsset& profile,
		Engine::UUID passID) {

		HierarchyItemLocation passLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass,
			passID, passLocation) || !passLocation.parentGroup) {

			return false;
		}
		const Engine::UUID parentGroupID = passLocation.parentGroup->id;
		HierarchyItem pass = std::move(
			(*passLocation.siblings)[passLocation.index]);
		passLocation.siblings->erase(
			passLocation.siblings->begin() + passLocation.index);

		HierarchyItemLocation groupLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group,
			parentGroupID, groupLocation)) {

			return false;
		}
		groupLocation.siblings->insert(groupLocation.siblings->begin() +
			groupLocation.index + 1, std::move(pass));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

	bool MoveItemToGroup(Engine::RenderFeatureProfileAsset& profile,
		HierarchyItemType type, Engine::UUID itemID,
		Engine::UUID targetGroupID) {

		if (type == HierarchyItemType::Group && itemID == targetGroupID) {
			return false;
		}
		const HierarchyItem* source = FindItem(profile.hierarchy, type, itemID);
		if (!source) {
			return false;
		}
		if (type == HierarchyItemType::Group &&
			FindItem(source->children, HierarchyItemType::Group,
				targetGroupID)) {

			return false;
		}

		HierarchyItemLocation sourceLocation{};
		if (!FindItemLocation(profile.hierarchy, type, itemID,
			sourceLocation)) {

			return false;
		}
		HierarchyItem moved = std::move(
			(*sourceLocation.siblings)[sourceLocation.index]);
		sourceLocation.siblings->erase(
			sourceLocation.siblings->begin() + sourceLocation.index);

		HierarchyItemLocation targetLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group,
			targetGroupID, targetLocation)) {

			return false;
		}
		(*targetLocation.siblings)[targetLocation.index].children.
			emplace_back(std::move(moved));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

	bool DrawApplicationSettings(
		Engine::RenderFeatureSelectionSettings& selection,
		bool drawAnchor) {

		bool changed = false;
		const Engine::RenderFeatureSelectionMode previousMode = selection.mode;
		changed |= Engine::MyGUI::EnumCombo(
			"適用方式", selection.mode).valueChanged;
		if (selection.mode ==
			Engine::RenderFeatureSelectionMode::Organization) {

			return changed;
		}
		if (previousMode ==
			Engine::RenderFeatureSelectionMode::Organization) {

			selection.renderingLayerMask = 1u;
			selection.phaseMask = Engine::MakeRenderFeaturePhaseMask(
				Engine::RenderPhase::Transparent);
			selection.rendererMask = Engine::RenderFeatureRendererMask::All;
		}
		if (drawAnchor) {
			changed |= Engine::MyGUI::EnumCombo(
				"実行位置", selection.anchor).valueChanged;
		}
		changed |= Engine::InspectorDrawerCommon::DrawLayerMaskField(
			"Rendering Layer", selection.renderingLayerMask).valueChanged;

		const auto drawPhase = [&](const char* label,
			Engine::RenderPhase phase) {

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
		ImGui::BeginDisabled(selection.mode ==
			Engine::RenderFeatureSelectionMode::IsolatedLayer);
		drawPhase("Opaque", Engine::RenderPhase::Opaque);
		ImGui::EndDisabled();
		drawPhase("Transparent", Engine::RenderPhase::Transparent);
		drawPhase("PostProcess UI",
			Engine::RenderPhase::PostProcessUI);

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

		if (selection.mode ==
			Engine::RenderFeatureSelectionMode::IsolatedLayer) {

			selection.phaseMask &= ~Engine::MakeRenderFeaturePhaseMask(
				Engine::RenderPhase::Opaque);
			changed |= Engine::MyGUI::DragInt(
				"合成レイヤー", selection.sortingLayer).valueChanged;
			changed |= Engine::MyGUI::DragInt(
				"合成順", selection.sortingOrder).valueChanged;
			changed |= Engine::MyGUI::EnumCombo(
				"合成方式", selection.compositeMode).valueChanged;
		}
		return changed;
	}
}

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================
void Engine::RenderFeatureProfileTool::DrawPassList(
	const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile =
		RenderFeatureProfileService::GetInstance().GetProfile();
	NormalizeRenderFeatureHierarchy(profile);
	const auto addPass = [&](RenderFeaturePassType type,
		std::string_view label, AssetID material = {},
		MaterialPassKind materialPass = MaterialPassKind::Invalid) {

		RenderFeaturePassSettings pass{};
		pass.id = UUID::New();
		pass.name = label;
		pass.type = type;
		pass.material = material;
		pass.materialPass = materialPass == MaterialPassKind::Invalid ?
			(type == RenderFeaturePassType::Compute ?
				MaterialPassKind::PostProcess : MaterialPassKind::RayTracing) :
			materialPass;
		pass.outputs.emplace_back();
		selectedPass_ = pass.id;
		selectedGroup_ = {};
		selectedPasses_ = { pass.id };
		profile.hierarchy.emplace_back(RenderFeatureHierarchyItem{
			.type = RenderFeatureHierarchyItemType::Pass,
			.id = pass.id,
		});
		profile.passes.emplace_back(std::move(pass));
		SetDirty();
	};
	const auto addMaterialPass = [&](AssetID sourceAsset,
		AssetType assetType = AssetType::Material,
		std::string_view assetPath = {}) {

		const AssetID materialID = ResolvePassMaterial(
			context, sourceAsset, assetType, assetPath);
		if (!context.panelContext ||
			!context.panelContext->renderPipeline || !materialID) {

			if (materialID) {
				statusMessage_ = "マテリアルを読み込めません";
				statusError_ = true;
			}
			return;
		}
		RenderAssetLibrary& assetLibrary = context.panelContext->
			renderPipeline->GetRenderAssetLibrary();
		assetLibrary.InvalidateMaterial(materialID);
		const MaterialAsset* material = assetLibrary.LoadMaterial(materialID);
		if (!material) {
			statusMessage_ = "マテリアルを読み込めません";
			statusError_ = true;
			return;
		}

		const MaterialPassBinding* postProcess = FindPass(
			*material, MaterialPassKind::PostProcess);
		const MaterialPassBinding* rayTracing = FindPass(
			*material, MaterialPassKind::RayTracing);
		const bool validPostProcess = postProcess &&
			postProcess->preferredVariant == PipelineVariantKind::Compute;
		const bool validRayTracing = rayTracing &&
			rayTracing->preferredVariant == PipelineVariantKind::Raytracing;
		const std::string name = MakeMaterialPassName(*material);
		if (material->domain == MaterialDomain::RayTracing &&
			validRayTracing) {

			addPass(RenderFeaturePassType::RayTracing, name, materialID,
				MaterialPassKind::RayTracing);
		} else if (validPostProcess) {

			addPass(RenderFeaturePassType::Compute, name, materialID,
				MaterialPassKind::PostProcess);
		} else if (validRayTracing) {

			addPass(RenderFeaturePassType::RayTracing, name, materialID,
				MaterialPassKind::RayTracing);
		} else {

			statusMessage_ =
				"ComputeまたはRayTracingパスがありません";
			statusError_ = true;
			return;
		}
		statusMessage_ = "マテリアルからパスを追加しました";
		statusError_ = false;
	};

	PendingAction pending{};
	std::function<void(std::vector<HierarchyItem>&)> drawItems =
		[&](std::vector<HierarchyItem>& items) {

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
				bool enabled = context.IsPlaying() ?
					overrides.IsEnabled(pass->id, pass->enabled) : pass->enabled;
				if (ImGui::Checkbox("##Enabled", &enabled)) {

					if (context.IsPlaying()) {

						overrides.SetEnabled(pass->id, enabled);
					} else {

						pass->enabled = enabled;
						SetDirty();
					}
				}
				ImGui::SameLine();
				const bool selected = IsSelected(selectedPasses_, item.id);
				if (ImGui::Selectable(pass->name.c_str(), selected,
					ImGuiSelectableFlags_SpanAvailWidth)) {

					if (ImGui::GetIO().KeyShift) {
						if (selected) {
							std::erase(selectedPasses_, item.id);
						} else {
							selectedPasses_.emplace_back(item.id);
						}
						selectedPass_ = selectedPasses_.empty() ?
							UUID{} : selectedPasses_.back();
					} else {
						selectedPass_ = item.id;
						selectedPasses_ = { item.id };
					}
					selectedGroup_ = {};
				}
				if (ImGui::BeginDragDropSource()) {
					const DragDropPayload payload{
						.type = HierarchyItemType::Pass,
						.id = item.id.value,
					};
					ImGui::SetDragDropPayload(kDragDropPayload,
						&payload, sizeof(payload));
					ImGui::TextUnformatted(pass->name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginPopupContextItem("PassContext")) {
					if (!IsSelected(selectedPasses_, item.id)) {
						selectedPass_ = item.id;
						selectedPasses_ = { item.id };
						selectedGroup_ = {};
					}
					const bool canGroup = CanGroupSelection(
						profile, selectedPasses_);
					if (ImGui::MenuItem("グループ化", nullptr,
						false, canGroup)) {

						pending.type = PendingActionType::GroupSelection;
					}
					HierarchyItemLocation location{};
					const bool grouped = FindItemLocation(profile.hierarchy,
						HierarchyItemType::Pass, item.id, location) &&
						location.parentGroup;
					if (ImGui::MenuItem("グループ化解除", nullptr,
						false, grouped)) {

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
				SetDirty();
			}
			ImGui::SameLine();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanAvailWidth;
			if (selectedGroup_ == item.id) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			const bool opened = ImGui::TreeNodeEx(
				"##Group", flags, "%s", item.name.c_str());
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
				ImGui::SetDragDropPayload(kDragDropPayload,
					&payload, sizeof(payload));
				ImGui::TextUnformatted(item.name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload =
					ImGui::AcceptDragDropPayload(kDragDropPayload)) {

					if (payload->IsDelivery() &&
						payload->DataSize == sizeof(DragDropPayload)) {

						const auto* dragged = static_cast<const DragDropPayload*>(
							payload->Data);
						pending = {
							.type = PendingActionType::MoveToGroup,
							.itemType = dragged->type,
							.item = UUID{ dragged->id },
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

	if (ImGui::BeginPopupContextWindow("RenderFeaturePassListContext",
		ImGuiPopupFlags_MouseButtonRight |
		ImGuiPopupFlags_NoOpenOverItems)) {

		if (ImGui::BeginMenu("追加")) {
			for (const auto [type, label] : {
				std::pair{ RenderFeaturePassType::Compute, "Compute" },
				std::pair{ RenderFeaturePassType::RayTracing, "DispatchRays" } }) {

				if (ImGui::MenuItem(label)) {
					addPass(type, label);
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
	if (dragging && dragging->IsDataType(
		IEditorPanel::kProjectAssetDragDropPayloadType) &&
		dragging->DataSize == sizeof(EditorAssetDragDropPayload)) {

		const auto* asset = static_cast<const EditorAssetDragDropPayload*>(
			dragging->Data);
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (asset && !asset->isDirectory && asset->assetID &&
			IsPassMaterialSource(asset->assetType, asset->assetPath) && window &&
			ImGui::BeginDragDropTargetCustom(window->InnerRect,
				window->GetID("##RenderFeatureAssetDropTarget"))) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				IEditorPanel::kProjectAssetDragDropPayloadType)) {

				if (payload->IsDelivery() && payload->DataSize ==
					sizeof(EditorAssetDragDropPayload)) {

					const auto* dropped =
						static_cast<const EditorAssetDragDropPayload*>(
							payload->Data);
					if (dropped && !dropped->isDirectory &&
						IsPassMaterialSource(
							dropped->assetType, dropped->assetPath)) {

						addMaterialPass(dropped->assetID,
							dropped->assetType, dropped->assetPath);
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
			SetDirty();
		}
		break;
	case PendingActionType::UngroupPass:
		if (UngroupPass(profile, pending.item)) {
			SetDirty();
		}
		break;
	case PendingActionType::DeleteItem:
		if (DeleteItem(profile, pending.itemType, pending.item)) {
			ClearSelection();
			SetDirty();
		}
		break;
	case PendingActionType::MoveToGroup:
		if (MoveItemToGroup(profile, pending.itemType,
			pending.item, pending.targetGroup)) {

			SetDirty();
		}
		break;
	case PendingActionType::None:
		break;
	}
}

bool Engine::RenderFeatureProfileTool::DrawSelectedPassControls(
	RenderFeatureProfileAsset& profile) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass,
		selectedPass_, location)) {

		ClearSelection();
		return false;
	}
	const float buttonWidth = ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
	ImGui::BeginDisabled(location.index == 0);
	if (ImGui::Button("上へ", ImVec2(buttonWidth, 0.0f))) {
		std::swap((*location.siblings)[location.index],
			(*location.siblings)[location.index - 1]);
		SynchronizeRenderFeaturePassOrder(profile);
		SetDirty();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(location.index + 1 >= location.siblings->size());
	if (ImGui::Button("下へ", ImVec2(buttonWidth, 0.0f))) {
		std::swap((*location.siblings)[location.index],
			(*location.siblings)[location.index + 1]);
		SynchronizeRenderFeaturePassOrder(profile);
		SetDirty();
	}
	ImGui::EndDisabled();
	if (ImGui::Button("パスを削除",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		DeleteItem(profile, HierarchyItemType::Pass, selectedPass_);
		ClearSelection();
		SetDirty();
		return false;
	}
	return true;
}

void Engine::RenderFeatureProfileTool::DrawSelectedGroupDetail(
	RenderFeatureProfileAsset& profile) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group,
		selectedGroup_, location)) {

		ClearSelection();
		return;
	}
	HierarchyItem& group = (*location.siblings)[location.index];
	bool changed = false;
	changed |= MyGUI::InputText("名前", group.name).valueChanged;
	changed |= MyGUI::Checkbox("有効", group.enabled);
	RenderFeatureSelectionSettings& selection = group.selection;
	const RenderFeatureAnchor previousAnchor = selection.anchor;
	changed |= DrawApplicationSettings(selection, true);
	if (selection.mode != RenderFeatureSelectionMode::Organization &&
		previousAnchor != selection.anchor) {

		std::unordered_set<uint64_t> passIDs{};
		CollectPassIDs(group, passIDs);
		for (RenderFeaturePassSettings& pass : profile.passes) {
			if (passIDs.contains(pass.id.value)) {
				pass.anchor = selection.anchor;
			}
		}
	}
	if (changed) {
		SetDirty();
	}
}

bool Engine::RenderFeatureProfileTool::DrawSelectedPassApplicationSettings(
	RenderFeatureProfileAsset& profile, RenderFeaturePassSettings& pass) {

	HierarchyItemLocation location{};
	if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass,
		pass.id, location)) {

		return false;
	}
	RenderFeatureSelectionSettings& selection =
		(*location.siblings)[location.index].selection;
	selection.anchor = pass.anchor;
	return DrawApplicationSettings(selection, false);
}
