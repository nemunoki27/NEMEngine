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

		// AssetTypeごとに受け付ける主な拡張子を返す
		// ドロップ先で何を渡せばよいか分かるようプレースホルダへ添えて使う
		std::string_view AcceptedExtensionsForType(AssetType type) {
			switch (type) {
			case AssetType::Texture:          return ".png/.jpg/.dds";
			case AssetType::Material:         return ".material";
			case AssetType::ShaderGraph:      return ".shadergraph";
			case AssetType::Mesh:             return ".gltf/.obj";
			case AssetType::Font:             return ".font";
			case AssetType::Audio:            return ".wav/.mp3";
			case AssetType::AnimationClip:    return ".animclip";
			case AssetType::Prefab:           return ".prefab";
			case AssetType::Scene:            return ".scene";
			case AssetType::Shader:           return ".hlsl/.shader";
			case AssetType::RenderPipeline:   return ".pipeline";
			case AssetType::Script:           return ".cs";
			case AssetType::PostProcessStack: return ".postProcessStack";
			default:                          return "";
			}
		}

		// 受付AssetType一覧から拡張子ヒント文字列を構築する
		std::string BuildAcceptedExtensionsHint(const std::initializer_list<AssetType>& acceptedTypes) {
			std::string hint;
			for (AssetType type : acceptedTypes) {

				const std::string_view extensions = AcceptedExtensionsForType(type);
				if (extensions.empty()) { continue; }
				if (!hint.empty()) { hint += " "; }
				hint += extensions;
			}
			return hint;
		}

		// アセット参照のラベルテキストを構築する
		std::string BuildAssetReferenceLabel(AssetID assetID, const AssetDatabase* assetDatabase,
			const std::initializer_list<AssetType>& acceptedTypes) {
			// アセットが未設定の場合は受付拡張子を添える
			if (!assetID) {
				const std::string hint = BuildAcceptedExtensionsHint(acceptedTypes);
				return hint.empty() ? "None (Drop asset here)" : std::format("None (Drop {} here)", hint);
			}
			// データベースがない場合はIDのみ表示
			if (!assetDatabase) { return std::format("GUID: {}", Engine::ToString(assetID)); }

			const AssetMeta* meta = assetDatabase->Find(assetID);
			// アセットが見つからないリンク切れ
			if (!meta) { return std::format("Missing Asset | GUID: {}", Engine::ToString(assetID)); }

			// アセット名を表示
			return std::format("Name: {}", MakeAssetDisplayNameFromPath(meta->assetPath));
		}

		// アセット参照のツールチップテキストを構築する
		std::string BuildAssetReferenceTooltip(AssetID assetID, const AssetDatabase* assetDatabase,
			const std::initializer_list<AssetType>& acceptedTypes) {
			if (!assetID) {
				const std::string hint = BuildAcceptedExtensionsHint(acceptedTypes);
				return hint.empty() ? "Drop asset here" : std::format("Drop asset here\nAccepted: {}", hint);
			}
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

	const bool hasValue = static_cast<bool>(value);
	// 値が未設定でも既定アセットが渡されていれば、描画で効く既定の名前を表示する
	const bool usesDefault = !hasValue && static_cast<bool>(setting.defaultAssetID);
	std::string displayText = BuildAssetReferenceLabel(value, assetDatabase, acceptedTypes);
	if (usesDefault) {

		const AssetMeta* defaultMeta = assetDatabase ? assetDatabase->Find(setting.defaultAssetID) : nullptr;
		const std::string defaultName = defaultMeta ?
			MakeAssetDisplayNameFromPath(defaultMeta->assetPath) : Engine::ToString(setting.defaultAssetID);
		displayText = std::format("Default: {}", defaultName);
	}

	// プレビュー画像の解決で設定がなければデータベースから検索
	const AssetID previewAssetID = hasValue ? value : setting.defaultAssetID;
	ImTextureID resolvedPreviewID = setting.previewTextureID;
	if (resolvedPreviewID == ImTextureID{}) {
		resolvedPreviewID = ResolveTextureAssetPreview(setting.graphicsCore, assetDatabase, previewAssetID);
	}

	// 完全な未設定時のみグレーアウトし、既定が効くときは通常表示にする
	if (!hasValue && !usesDefault) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }

	float buttonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - setting.reserveRightWidth);
	ImVec2 button = setting.buttonSize.has_value() ? setting.buttonSize.value() : ImVec2(buttonWidth, ImGui::GetFrameHeight());
	ImGui::Button(displayText.c_str(), button);

	if (!hasValue && !usesDefault) { ImGui::PopStyleColor(); }

	result.anyItemActive = ImGui::IsItemActive();

	// 右クリックメニューによる削除機能
	if (hasValue && setting.allowDelete && ImGui::BeginPopupContextItem("##assetRefDelete")) {
		if (ImGui::MenuItem("削除")) { value = AssetID{}; result.valueChanged = true; result.editFinished = true; }
		ImGui::EndPopup();
	}

	// ツールチップとプレビュー画像の表示
	if (setting.showTooltip && ImGui::BeginItemTooltip()) {
		const std::string tooltip = BuildAssetReferenceTooltip(value, assetDatabase, acceptedTypes);
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
