#include "InspectorPrefabSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <unordered_set>

// imgui
#include <imgui.h>

//============================================================================
//	InspectorPrefabSession classMethods
//============================================================================

void Engine::InspectorPrefabSession::Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// プレファブ編集中はUIを表示しない
	if (!context.editorContext || context.editorContext->isPrefabEditing) {
		return;
	}

	AssetDatabase* database = context.editorContext->assetDatabase;
	if (!database || !world.HasComponent<PrefabLinkComponent>(entity)) {
		return;
	}
	// Play中は実行状態をプレファブ差分として計算しない
	if (context.IsPlaying()) {
		ImGui::TextDisabled("Prefab上書きはPlay停止後に操作できます");
		ImGui::Spacing();
		return;
	}

	// 差分一覧を開くまで所属同期と差分計算を行わない
	if (ImGui::Button("Prefab 上書きパラメータ###PrefabOverrideButton", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		overrideChoices_.clear();
		prefabBaseCache_.Clear();
		ImGui::OpenPopup("PrefabOverridesPopup");
	}
	ImGui::Spacing();
	if (!ImGui::BeginPopup("PrefabOverridesPopup")) {
		prefabBaseCache_.Clear();
		return;
	}

	const PrefabLinkComponent link = world.GetComponent<PrefabLinkComponent>(entity);

	// 一覧表示に必要なインスタンス差分を取得
	const auto baseSnapshot = prefabBaseCache_.Load(*database, link.prefabAsset);
	if (!baseSnapshot) {
		ImGui::TextWrapped("%s", "Prefabの基準データを読み込めません");
		ImGui::EndPopup();
		return;
	}
	const auto& base = *baseSnapshot;
	PrefabOverrideUtility::SynchronizeNestedPrefabOwnership(world);
	PrefabInstanceData data = PrefabOverrideUtility::CaptureInstance(world, *database, link.prefabInstanceID, base);
	if (!data.instanceID) {
		ImGui::TextWrapped("%s", "Prefabの差分を取得できません。参照先のAssetを確認してください");
		ImGui::EndPopup();
		return;
	}
	data.prefabAsset = link.prefabAsset;
	const UUID sceneInstanceID = world.HasComponent<SceneObjectComponent>(entity)
									 ? world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID
									 : UUID{};

	// SceneローカルIDから同じSceneのEntityを引く
	auto findSceneEntity = [&](UUID localFileID) -> Entity {
		Entity found = Entity::Null();
		world.ForEach<SceneObjectComponent>([&](const Entity& candidate, SceneObjectComponent& sceneObject) {
			if (!found.IsValid() && sceneObject.sceneInstanceID == sceneInstanceID && sceneObject.localFileID == localFileID) {
				found = candidate;
			}
		});
		return found;
	};

	// 追加Entityは親も追加Entityなら子なのでルートだけを一覧に出す
	std::unordered_set<UUID> addedSceneLocalFileIDs;
	for (const auto& added : data.addedEntities) {
		addedSceneLocalFileIDs.insert(added.sceneLocalFileID);
	}
	std::vector<Entity> addedEntityRoots;
	for (const auto& added : data.addedEntities) {

		if (addedSceneLocalFileIDs.contains(added.parentSceneLocalFileID)) {
			continue;
		}
		const Entity addedRoot = findSceneEntity(added.sceneLocalFileID);
		if (world.IsAlive(addedRoot)) {
			addedEntityRoots.emplace_back(addedRoot);
		}
	}

	const int overrideCount =
		static_cast<int>(data.modifications.size() + data.addedComponents.size() + data.removedComponents.size() +
						 addedEntityRoots.size() + data.removedEntities.size());

	// 件数は差分を計算した一覧内だけに表示
	ImGui::TextDisabled("上書きパラメータ: %d", overrideCount);

	const bool applyClicked = ImGui::Button("設定を適用");
	ImGui::Separator();

	// 各差分の選択ボタンを描画する、アクティブな選択を青で強調しデフォルトはそのまま
	auto drawChoice = [&](const std::string& key, bool allowApply) {
		int& choice = overrideChoices_[key];
		auto button = [&](const char* label, int value, bool enabled) {
			const bool active = (choice == value);
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.95f, 1.0f));
			}
			if (!enabled) {
				ImGui::BeginDisabled();
			}
			if (ImGui::SmallButton((std::string(label) + "##" + key).c_str())) {
				choice = value;
			}
			if (!enabled) {
				ImGui::EndDisabled();
			}
			if (active) {
				ImGui::PopStyleColor();
			}
		};
		button("プレファブへ反映", 1, allowApply);
		ImGui::SameLine();
		button("元に戻す", 2, true);
		ImGui::SameLine();
		button("このインスタンスのみ維持", 0, true);
	};

	if (overrideCount == 0) {
		ImGui::TextDisabled("差分はありません");
	}

	// プロパティ差分
	for (const auto& mod : data.modifications) {

		const std::string key = "M|" + ToString(mod.target) + "|" + mod.path;
		const nlohmann::json* baseValue = nullptr;
		auto baseIt = base.find(mod.target);
		if (baseIt != base.end()) {
			baseValue = PrefabJsonDiff::GetAtPath(baseIt->second.components, mod.path);
		}
		ImGui::TextUnformatted(mod.path.c_str());
		const std::string valueText = (baseValue ? baseValue->dump() : std::string("(none)")) + "  ->  " + mod.value.dump();
		ImGui::TextDisabled("%s", valueText.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 追加コンポーネント
	for (const auto& added : data.addedComponents) {

		const std::string key = "AC|" + ToString(added.target) + "|" + added.type;
		ImGui::Text("+ %s  追加コンポーネント", added.type.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 削除コンポーネント
	for (const auto& removed : data.removedComponents) {

		const std::string key = "RC|" + ToString(removed.target) + "|" + removed.type;
		ImGui::Text("- %s  削除コンポーネント", removed.type.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 追加Entity
	for (const Entity& addedRoot : addedEntityRoots) {

		const UUID localFileID = world.GetComponent<SceneObjectComponent>(addedRoot).localFileID;
		const std::string key = "AE|" + ToString(localFileID);
		const std::string name =
			world.HasComponent<NameComponent>(addedRoot) ? world.GetComponent<NameComponent>(addedRoot).name : "Entity";
		const bool canApply = PrefabOverrideUtility::CanPromoteAddedEntitySubtree(world, addedRoot, link.prefabInstanceID);
		ImGui::Text("+ %s  追加Entity", name.c_str());
		drawChoice(key, canApply);
		if (!canApply) {
			ImGui::TextDisabled("Nested Prefabを含むサブツリーは反映できません");
		}
		ImGui::Separator();
	}
	// 旧Sceneの削除差分は復元用に読み込みを維持する
	for (size_t i = 0; i < data.removedEntities.size(); ++i) {
		ImGui::TextDisabled("- 取り除かれた子エンティティ");
	}

	// 適用ボタンで各差分の選択を反映する
	if (applyClicked && ApplySelection(context, world, *database, link, base, data, addedEntityRoots)) {
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::InspectorPrefabSession::Clear() {

	prefabBaseCache_.Clear();
}

bool Engine::InspectorPrefabSession::ApplySelection(const EditorPanelContext& context, ECSWorld& world, AssetDatabase& database,
	const PrefabLinkComponent& link, const PrefabBaseEntities& base, const PrefabInstanceData& data,
	const std::vector<Entity>& addedEntityRoots) {

	std::vector<UUID> selectedUUIDs;
	if (context.editorState) {
		selectedUUIDs.reserve(context.editorState->selectedEntities.size());
		for (const Entity& selected : context.editorState->selectedEntities) {
			if (world.IsAlive(selected)) {
				selectedUUIDs.emplace_back(world.GetUUID(selected));
			}
		}
	}

	// インスタンス内の対象エンティティを引く
	auto findInstanceEntity = [&](UUID target) -> Entity {
		for (const Entity& candidate : PrefabOverrideUtility::CollectInstanceEntities(world, link.prefabInstanceID)) {
			if (world.GetComponent<PrefabLinkComponent>(candidate).prefabLocalFileID == target) {
				return candidate;
			}
		}
		return Entity::Null();
	};

	// プレファブファイルを読み、Apply対象を書き込む
	const auto prefabPath = database.ResolveFullPath(link.prefabAsset);
	nlohmann::json prefabFileJson = JsonAdapter::Load(prefabPath.string(), true);
	const auto& oldBase = base;
	bool prefabChanged = false;
	bool instanceHierarchyChanged = false;
	std::vector<PrefabPropertyModification> propertiesToRevert;
	std::vector<PrefabComponentModification> addedComponentsToRevert;
	std::vector<PrefabComponentModification> removedComponentsToRevert;

	for (const auto& mod : data.modifications) {

		const std::string key = "M|" + ToString(mod.target) + "|" + mod.path;
		const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
		if (choice == 1) {

			prefabChanged |= PrefabOverrideUtility::SetPrefabEntityLeaf(prefabFileJson, mod.target, mod.path, mod.value);
		} else if (choice == 2) {
			propertiesToRevert.emplace_back(mod);
		}
	}
	for (const auto& added : data.addedComponents) {

		const std::string key = "AC|" + ToString(added.target) + "|" + added.type;
		const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
		if (choice == 1) {

			prefabChanged |=
				PrefabOverrideUtility::SetPrefabEntityComponent(prefabFileJson, added.target, added.type, added.value);
		} else if (choice == 2) {
			addedComponentsToRevert.emplace_back(added);
		}
	}
	for (const auto& removed : data.removedComponents) {

		const std::string key = "RC|" + ToString(removed.target) + "|" + removed.type;
		const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
		if (choice == 1) {

			prefabChanged |= PrefabOverrideUtility::RemovePrefabEntityComponent(prefabFileJson, removed.target, removed.type);
		} else if (choice == 2) {
			removedComponentsToRevert.emplace_back(removed);
		}
	}

	// 追加Entityはサブツリー単位でPrefabへ反映または破棄する
	std::vector<Entity> addedRootsToApply;
	std::vector<Entity> addedRootsToRevert;
	for (const Entity& addedRoot : addedEntityRoots) {

		if (!world.IsAlive(addedRoot) || !world.HasComponent<SceneObjectComponent>(addedRoot)) {
			continue;
		}
		const UUID localFileID = world.GetComponent<SceneObjectComponent>(addedRoot).localFileID;
		const std::string key = "AE|" + ToString(localFileID);
		const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
		if (choice == 1 && PrefabOverrideUtility::CanPromoteAddedEntitySubtree(world, addedRoot, link.prefabInstanceID)) {
			addedRootsToApply.emplace_back(addedRoot);
		} else if (choice == 2) {
			addedRootsToRevert.emplace_back(addedRoot);
		}
	}
	auto rollbackPromotedLinks = [&]() {
		// 保存前に追加したPrefabリンクだけを取り消す
		for (const Entity& addedRoot : addedRootsToApply) {
			for (const Entity& promoted : EditorEntitySnapshotUtility::CollectSubtreeEntities(world, addedRoot)) {

				if (world.IsAlive(promoted) && world.HasComponent<PrefabLinkComponent>(promoted)) {
					world.RemoveComponentByName(promoted, "PrefabLink");
				}
			}
		}
	};
	if (!addedRootsToApply.empty()) {

		try {
			prefabChanged |= PrefabOverrideUtility::PromoteAddedEntitySubtrees(
				prefabFileJson, world, link.prefabAsset, link.prefabInstanceID, addedRootsToApply);
		} catch (...) {
			rollbackPromotedLinks();
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] 追加EntityのPrefab昇格に失敗したため反映を中止しました path={}", prefabPath.string());
			return false;
		}
	}
	bool applySucceeded = true;
	if (prefabChanged) {
		try {
			// 正規化と保存が完了するまでライブリンクを確定しない
			PrefabReferenceRemapper::NormalizePrefabFileHierarchy(prefabFileJson);
			PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(prefabFileJson);
			applySucceeded = JsonAdapter::SaveCanonical(prefabPath, prefabFileJson);
			if (applySucceeded) {
				database.NotifyContentChanged(link.prefabAsset);
				prefabBaseCache_.Clear();
			}
		} catch (...) {
			applySucceeded = false;
		}
		if (!applySucceeded) {

			rollbackPromotedLinks();
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] Prefabアセットを保存できなかったため反映を中止しました path={}", prefabPath.string());
		}
	}
	if (applySucceeded) {

		// ファイル保存が必要な操作は保存成功後にだけライブEntityへ反映する
		for (const auto& mod : propertiesToRevert) {

			const Entity target = findInstanceEntity(mod.target);
			auto baseIt = oldBase.find(mod.target);
			if (!world.IsAlive(target) || baseIt == oldBase.end()) {
				continue;
			}
			const nlohmann::json* baseValue = PrefabJsonDiff::GetAtPath(baseIt->second.components, mod.path);
			if (!baseValue) {
				continue;
			}
			const size_t slash = mod.path.find('/');
			const std::string type = slash == std::string::npos ? mod.path : mod.path.substr(0, slash);
			const std::string leaf = slash == std::string::npos ? std::string{} : mod.path.substr(slash + 1);
			nlohmann::json current;
			world.SerializeComponentToJson(target, type, current);
			PrefabJsonDiff::SetAtPath(current, leaf, *baseValue);
			world.AddComponentFromJson(target, type, current);
		}
		for (const auto& added : addedComponentsToRevert) {

			const Entity target = findInstanceEntity(added.target);
			if (world.IsAlive(target)) {
				world.RemoveComponentByName(target, added.type);
			}
		}
		for (const auto& removed : removedComponentsToRevert) {

			const Entity target = findInstanceEntity(removed.target);
			auto baseIt = oldBase.find(removed.target);
			if (world.IsAlive(target) && baseIt != oldBase.end() && baseIt->second.components.contains(removed.type)) {

				world.AddComponentFromJson(target, removed.type, baseIt->second.components[removed.type]);
			}
		}
		for (const Entity& addedRoot : addedRootsToRevert) {

			if (world.IsAlive(addedRoot)) {
				EditorEntitySnapshotUtility::DestroySubtree(world, addedRoot);
				instanceHierarchyChanged = true;
			}
		}
	}
	if (instanceHierarchyChanged && !prefabChanged) {

		std::vector<Entity> hierarchyScope;
		world.ForEachAliveEntity([&](Entity candidate) { hierarchyScope.emplace_back(candidate); });
		HierarchySystem hierarchySystem{};
		hierarchySystem.RebuildRuntimeLinks(world, hierarchyScope);
	}
	if (prefabChanged && applySucceeded) {

		// 変更を全インスタンスへ伝播し、関係ないOverrideは保持する
		HierarchySystem hierarchySystem{};
		applySucceeded =
			PrefabOverrideUtility::PropagateToInstances(world, database, hierarchySystem, link.prefabAsset, oldBase);
	}
	if (context.editorState && !selectedUUIDs.empty()) {

		std::vector<Entity> restoredSelection;
		restoredSelection.reserve(selectedUUIDs.size());
		for (UUID stableUUID : selectedUUIDs) {
			const Entity selected = world.FindByUUID(stableUUID);
			if (world.IsAlive(selected)) {
				restoredSelection.emplace_back(selected);
			}
		}
		context.editorState->SetSelectedEntities(restoredSelection);
	}
	return applySucceeded;
}
