#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++

namespace Engine::ScriptFieldInspector {

	Engine::ValueEditResult DrawAssetRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		if (!value.is_object()) { value = nlohmann::json{ {"assetId", ""} }; }
		Engine::AssetID assetID = Engine::FromString32Hex(value.value("assetId", std::string{}));

		const Engine::AssetDatabase* db = (ctx.panel && ctx.panel->editorContext) ? ctx.panel->editorContext->assetDatabase : nullptr;
		Engine::ValueEditResult r = Engine::MyGUI::AssetReferenceField(label, assetID, db,
			{ AssetTypeFromName(field.assetType) });
		if (r.valueChanged) {
			value["assetId"] = assetID ? Engine::ToString(assetID) : std::string{};
		}
		return r;
	}

	std::string ResolveEntityRefName(Engine::ECSWorld* world, const std::string& localFileID) {

		if (!world || localFileID.empty()) { return {}; }
		std::string name;
		world->ForEach<Engine::SceneObjectComponent>([&](Engine::Entity e, Engine::SceneObjectComponent& so) {
			if (name.empty() && Engine::ToString(so.localFileID) == localFileID) {
				const Engine::NameComponent* nameComponent = world->TryGetComponent<Engine::NameComponent>(e);
				name = nameComponent ? nameComponent->name : std::string("Entity");
			}
			});
		return name;
	}

	Engine::ValueEditResult DrawEntityRef(const char* label, nlohmann::json& value, const DrawContext& ctx,
		const EntityDropFilter& dropFilter, const char* rejectTooltip) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }; }

		const std::string kind = value.value("kind", std::string("Null"));
		const std::string localFileID = value.value("localFileId", std::string{});

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		// AssetRefと同じ見た目に合わせる、行幅いっぱいのボタンで未設定はグレーアウトする
		const bool hasValue = !(kind == "Null" || localFileID.empty());
		std::string preview;
		if (!hasValue) {
			preview = "None (Drop entity here)";
		} else {
			// AssetRefと同じくName表示にし、解決できなければ欠落表示にする
			const std::string name = ResolveEntityRefName(ctx.world, localFileID);
			preview = name.empty() ? ("Missing Entity | " + localFileID) : ("Name: " + name);
		}

		ImGui::PushID(label);
		if (!hasValue) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
		ImGui::Button(preview.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		if (!hasValue) { ImGui::PopStyleColor(); }
		result.anyItemActive = ImGui::IsItemActive();

		// 解除はClearボタンではなくAssetRefと同じ右クリックの削除メニューで行う
		if (hasValue && ImGui::BeginPopupContextItem("##entityRefDelete")) {
			if (ImGui::MenuItem("削除")) {
				value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndPopup();
		}

		// ドラッグされたエンティティを参照に設定する、フィルタを通らないエンティティは受け付けない
		if (ImGui::BeginDragDropTarget()) {

			// Accept前にpayloadを覗いて判定する、受け入れない場合はハイライトを出さずUnityの禁止表示相当にする
			bool acceptable = true;
			if (dropFilter && ctx.world) {
				const ImGuiPayload* hovered = ImGui::GetDragDropPayload();
				if (hovered && hovered->IsDataType(Engine::IEditorPanel::kHierarchyDragDropPayloadType) &&
					hovered->DataSize == sizeof(Engine::UUID)) {

					Engine::UUID draggedUUID = *static_cast<const Engine::UUID*>(hovered->Data);
					Engine::Entity target = ctx.world->FindByUUID(draggedUUID);
					acceptable = ctx.world->IsAlive(target) && dropFilter(*ctx.world, target);
					if (!acceptable && rejectTooltip) {
						ImGui::SetTooltip("%s", rejectTooltip);
					}
				}
			}

			if (acceptable) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
					if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID) && ctx.world) {

						Engine::UUID draggedUUID = *static_cast<const Engine::UUID*>(payload->Data);
						Engine::Entity target = ctx.world->FindByUUID(draggedUUID);
						if (ctx.world->IsAlive(target) && ctx.world->HasComponent<Engine::SceneObjectComponent>(target)) {

							const auto& sceneObject = ctx.world->GetComponent<Engine::SceneObjectComponent>(target);
							value["kind"] = "Scene";
							value["sourceAsset"] = sceneObject.sourceAsset ? Engine::ToString(sceneObject.sourceAsset) : std::string{};
							value["localFileId"] = Engine::ToString(sceneObject.localFileID);
							result.valueChanged = true;
							result.editFinished = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	Engine::ValueEditResult DrawComponentRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// 対象コンポーネントを持つエンティティだけドロップを受け入れる
		const std::string rejectTooltip = field.componentType + " を持っていません";
		Engine::ValueEditResult entityResult = DrawEntityRef(label, value["entity"], ctx,
			[&field](Engine::ECSWorld& world, Engine::Entity target) {
				if (field.componentType.empty() || world.HasComponent(target, field.componentType)) {
					return true;
				}
				// componentTypeはwrapper型名で、登録名と異なる場合がある(例 CollisionComponent → Collision)
				const std::string suffix = "Component";
				if (field.componentType.size() > suffix.size() &&
					field.componentType.compare(field.componentType.size() - suffix.size(), suffix.size(), suffix) == 0) {
					return world.HasComponent(target, field.componentType.substr(0, field.componentType.size() - suffix.size()));
				}
				return false;
			}, rejectTooltip.c_str());
		result.valueChanged |= entityResult.valueChanged;
		result.anyItemActive |= entityResult.anyItemActive;
		result.editFinished |= entityResult.editFinished;
		return result;
	}

	std::string ToShortTypeName(const std::string& typeName) {

		const size_t pos = typeName.find_last_of('.');
		return pos == std::string::npos ? typeName : typeName.substr(pos + 1);
	}

	bool MatchesFieldScriptType(const Engine::ScriptEntry& slotEntry, const Engine::ManagedFieldSchema& field) {

		if (field.scriptType.empty()) {
			return true;
		}
		if (slotEntry.scriptTypeID.empty()) {
			return false;
		}
		const auto* info = Engine::BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(slotEntry.scriptTypeID);
		return info && info->name == field.scriptType;
	}

	Engine::ValueEditResult DrawScriptRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// 所有エンティティの参照部分、対象スクリプトを持つエンティティだけドロップを受け入れる
		const std::string rejectTooltip = ToShortTypeName(field.scriptType) + " を持っていません";
		Engine::ValueEditResult ownerResult = DrawEntityRef(label, value["entity"], ctx,
			[&field](Engine::ECSWorld& world, Engine::Entity target) {
				if (!world.HasComponent<Engine::ScriptComponent>(target)) {
					return false;
				}
				for (const Engine::ScriptEntry& slotEntry :
					Engine::GetScriptEntries(world, target)) {
					if (MatchesFieldScriptType(slotEntry, field)) {
						return true;
					}
				}
				return false;
			}, rejectTooltip.c_str());
		if (ownerResult.valueChanged) {
			result.valueChanged = true;
			// 所有エンティティが変わったら旧エンティティのスロット選択を破棄する
			value["scriptSlotId"] = "";
			value["scriptTypeId"] = "";
		}
		result.anyItemActive |= ownerResult.anyItemActive;

		// 所有エンティティ上の同型スロットを選ばせる
		const std::string ownerLocal = value["entity"].value("localFileId", std::string{});
		if (!ownerLocal.empty() && ctx.world) {
			// 対象エンティティをlocalFileIDで探す
			Engine::Entity owner = Engine::Entity::Null();
			ctx.world->ForEach<Engine::SceneObjectComponent>([&](Engine::Entity e, Engine::SceneObjectComponent& so) {
				if (Engine::ToString(so.localFileID) == ownerLocal) { owner = e; }
				});

			if (ctx.world->IsAlive(owner) && ctx.world->HasComponent<Engine::ScriptComponent>(owner)) {

				const std::span<const Engine::ScriptEntry> scriptEntries =
					Engine::GetScriptEntries(*ctx.world, owner);

				// ドロップ直後は一致スロットが1つだけなら自動選択する、複数あるときだけComboで選ばせる
				if (ownerResult.valueChanged) {
					const Engine::ScriptEntry* matched = nullptr;
					int matchCount = 0;
					for (const Engine::ScriptEntry& slotEntry : scriptEntries) {
						if (MatchesFieldScriptType(slotEntry, field)) {
							matched = &slotEntry;
							++matchCount;
						}
					}
					if (matchCount == 1) {
						value["scriptSlotId"] = Engine::ToString(matched->scriptSlotID);
						value["scriptTypeId"] = matched->scriptTypeID;
						result.editFinished = true;
					}
				}

				const std::string currentSlot = value.value("scriptSlotId", std::string{});

				if (Engine::MyGUI::BeginPropertyRow("  対象スクリプト")) {
					// 候補は型名で表示する、内部IDは分かりにくいので既定プレビューには出さない
					std::string preview = "未選択";
					for (const Engine::ScriptEntry& slotEntry : scriptEntries) {
						if (Engine::ToString(slotEntry.scriptSlotID) == currentSlot) {
							preview = ToShortTypeName(slotEntry.lastKnownTypeName);
							break;
						}
					}
					if (ImGui::BeginCombo("##slot", preview.c_str())) {
						// 同型が複数あるときの区別用に候補の通し番号を振る
						int candidateOrder = 0;
						for (const Engine::ScriptEntry& slotEntry : scriptEntries) {
							// 型が一致するスロットのみ候補にする
							if (!MatchesFieldScriptType(slotEntry, field)) {
								continue;
							}
							++candidateOrder;
							const std::string slotID = Engine::ToString(slotEntry.scriptSlotID);
							// 内部IDは見せず名前空間を除いた型名と通し番号で表示する
							const std::string itemLabel = ToShortTypeName(slotEntry.lastKnownTypeName) + " #" + std::to_string(candidateOrder);
							if (ImGui::Selectable(itemLabel.c_str(), slotID == currentSlot)) {
								value["scriptSlotId"] = slotID;
								value["scriptTypeId"] = slotEntry.scriptTypeID;
								result.valueChanged = true;
								result.editFinished = true;
							}
						}
						ImGui::EndCombo();
					}
					Engine::MyGUI::EndPropertyRow();
				}
			}
		}
		return result;
	}
}
