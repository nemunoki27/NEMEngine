#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>

// c++
#include <algorithm>
#include <format>

namespace Engine {
	namespace {

		// アセット参照のラベルテキストを構築する
		std::string BuildAssetReferenceLabel(AssetID assetID, const AssetDatabase* assetDatabase) {
			// アセットが未設定の場合
			if (!assetID) { return "None (Drop asset here)"; }
			// データベースがない場合はIDのみ表示
			if (!assetDatabase) { return std::format("GUID: {}", Engine::ToString(assetID)); }

			const AssetMeta* meta = assetDatabase->Find(assetID);
			// アセットが見つからないリンク切れ
			if (!meta) { return std::format("Missing Asset | GUID: {}", Engine::ToString(assetID)); }

			// アセット名を表示
			return std::format("Name: {}", MakeAssetDisplayNameFromPath(meta->assetPath));
		}

		// アセット参照のツールチップテキストを構築する
		std::string BuildAssetReferenceTooltip(AssetID assetID, const AssetDatabase* assetDatabase) {
			if (!assetID) { return "Drop asset here"; }
			if (!assetDatabase) { return std::format("GUID: {}", Engine::ToString(assetID)); }
			const AssetMeta* meta = assetDatabase->Find(assetID);
			if (!meta) { return std::format("Missing Asset\nGUID: {}", Engine::ToString(assetID)); }
			// パスとGUIDを含む詳細情報をツールチップに表示
			return std::format("Name : {}\nPath : {}\nGUID : {}", MakeAssetDisplayNameFromPath(meta->assetPath), meta->assetPath, Engine::ToString(assetID));
		}

		// エンティティ参照のラベルテキストを構築する
		std::string BuildEntityReferenceLabel(UUID entityUUID, ECSWorld* world) {
			if (!entityUUID) { return "None (Drop entity here)"; }
			if (!world) { return std::format("UUID: {}", Engine::ToString(entityUUID)); }

			const Entity entity = world->FindByUUID(entityUUID);
			if (!world->IsAlive(entity)) { return std::format("Missing Entity | UUID: {}", Engine::ToString(entityUUID)); }

			if (NameComponent* name = world->TryGetComponent<NameComponent>(entity)) {
				return std::format("Name: {}", name->name);
			}
			return std::format("Entity ({})", Engine::ToString(entityUUID));
		}

		// エンティティ参照のツールチップテキストを構築する
		std::string BuildEntityReferenceTooltip(UUID entityUUID, ECSWorld* world) {
			if (!entityUUID) { return "Drop hierarchy entity here"; }
			if (!world) { return std::format("UUID: {}", Engine::ToString(entityUUID)); }

			const Entity entity = world->FindByUUID(entityUUID);
			if (!world->IsAlive(entity)) { return std::format("Missing Entity\nUUID: {}", Engine::ToString(entityUUID)); }

			if (NameComponent* name = world->TryGetComponent<NameComponent>(entity)) {
				return std::format("Name : {}\nUUID : {}", name->name, Engine::ToString(entityUUID));
			}
			return std::format("UUID : {}", Engine::ToString(entityUUID));
		}

	} // namespace
} // Engine

Engine::ValueEditResult Engine::MyGUI::AssetReferenceField(const char* label, AssetID& value, const AssetDatabase* assetDatabase, const std::initializer_list<AssetType>& acceptedTypes, const AssetEditSetting& setting) {
	ValueEditResult result{};
	// 行の開始
	if (setting.useAutoPropertyRow) { if (!BeginPropertyRow(label, setting.propertyRow)) { return result; } }

	const std::string displayText = BuildAssetReferenceLabel(value, assetDatabase);
	const bool hasValue = static_cast<bool>(value);

	// プレビュー画像の解決で設定がなければデータベースから検索
	ImTextureID resolvedPreviewID = setting.previewTextureID;
	if (resolvedPreviewID == ImTextureID{}) {
		resolvedPreviewID = ResolveTextureAssetPreview(setting.graphicsCore, assetDatabase, value);
	}

	// 未設定時はグレーアウト
	if (!hasValue) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }

	float buttonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - setting.reserveRightWidth);
	ImVec2 button = setting.buttonSize.has_value() ? setting.buttonSize.value() : ImVec2(buttonWidth, ImGui::GetFrameHeight());
	ImGui::Button(displayText.c_str(), button);

	if (!hasValue) { ImGui::PopStyleColor(); }

	result.anyItemActive = ImGui::IsItemActive();

	// 右クリックメニューによる削除機能
	if (hasValue && setting.allowDelete && ImGui::BeginPopupContextItem("##assetRefDelete")) {
		if (ImGui::MenuItem("削除")) { value = AssetID{}; result.valueChanged = true; result.editFinished = true; }
		ImGui::EndPopup();
	}

	// ツールチップとプレビュー画像の表示
	if (setting.showTooltip && ImGui::BeginItemTooltip()) {
		const std::string tooltip = BuildAssetReferenceTooltip(value, assetDatabase);
		ImGui::TextUnformatted(tooltip.c_str());
		if (resolvedPreviewID != ImTextureID{}) {
			ImGui::Image(resolvedPreviewID, ImVec2(128.0f, 128.0f));
		}
		ImGui::EndTooltip();
	}

	// ドラッグ＆ドロップ受け入れ処理
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery()) {
				EditorAssetDragDropPayload assetPayload{};
				if (TryReadAssetPayload(payload, assetPayload)) {
					// ドロップされたアセットの型を解決しメタデータがなければ推測
					AssetType assetType = assetPayload.assetType;
					if (assetType == AssetType::Unknown && assetDatabase) {
						if (const AssetMeta* meta = assetDatabase->Find(assetPayload.assetID)) { assetType = meta->type; }
					}
					if (assetType == AssetType::Unknown) { assetType = GuessDroppedAssetType(assetPayload); }

					// 許可された型であれば値を更新
					if (IsAcceptedAssetType(assetType, acceptedTypes)) {
						if (value != assetPayload.assetID) {
							value = assetPayload.assetID; result.valueChanged = true; result.editFinished = true;
						}
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (setting.useAutoPropertyRow) { EndPropertyRow(); }
	return result;
}

Engine::ValueEditResult Engine::MyGUI::EntityReferenceField(const char* label, UUID& value, ECSWorld* world, const EntityEditSetting& setting) {
	ValueEditResult result{};
	if (setting.useAutoPropertyRow) { if (!BeginPropertyRow(label, setting.propertyRow)) { return result; } }

	ImGui::PushID(label);
	const std::string displayText = BuildEntityReferenceLabel(value, world);
	const bool hasValue = static_cast<bool>(value);

	if (!hasValue) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
	const ImVec2 button = setting.buttonSize.has_value() ? setting.buttonSize.value() : ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
	ImGui::Button(displayText.c_str(), button);
	if (!hasValue) { ImGui::PopStyleColor(); }

	result.anyItemActive = ImGui::IsItemActive();

	// ツールチップ表示
	if (ImGui::BeginItemTooltip()) {
		const std::string tooltip = BuildEntityReferenceTooltip(value, world);
		ImGui::TextUnformatted(tooltip.c_str());
		ImGui::EndTooltip();
	}

	// ヒエラルキーからのドラッグ＆ドロップ受け入れでUUIDベースで紐付け
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {
				UUID droppedUUID{};
				if (TryReadEntityPayload(payload, droppedUUID)) {
					// 同一ワールド内に存在し、生存しているか確認
					const Entity droppedEntity = world ? world->FindByUUID(droppedUUID) : Entity::Null();
					if (world && world->IsAlive(droppedEntity) && value != droppedUUID) {
						value = droppedUUID; result.valueChanged = true; result.editFinished = true;
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::PopID();
	if (setting.useAutoPropertyRow) { EndPropertyRow(); }
	return result;
}
