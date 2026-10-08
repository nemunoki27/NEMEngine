#include "SceneCompositionOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <unordered_set>

bool Engine::SceneCompositionOperations::ApplyChanges(const EditorToolContext& context,
	SceneInstance& instance, const std::vector<SubSceneSlotDesc>& proposed, std::string& statusMessage, bool& statusError) {

	const std::vector<SubSceneSlotDesc> previous = instance.header.subScenes;
	instance.header.subScenes = proposed;
	const UUID instanceID = instance.instanceID;
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	bool valid = true;
	for (const SubSceneSlotDesc& slot : instance.header.subScenes) {

		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second ||
			(slot.sceneAsset && slot.sceneAsset == instance.sceneAsset)) {
			valid = false;
			break;
		}
	}

	SceneInstanceManager* scenes = context.toolContext.sceneInstances;
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	ECSWorld* world = context.GetWorld();
	if (!valid || !scenes || !assetDatabase || !world ||
		!scenes->SynchronizeSubScenes(
			*assetDatabase, SceneSystem{}, *world, instanceID)) {

		SceneInstance* current = scenes ? scenes->Find(instanceID) : nullptr;
		if (current) {
			current->header.subScenes = previous;
		}
		if (scenes && assetDatabase && world) {
			scenes->SynchronizeSubScenes(
				*assetDatabase, SceneSystem{}, *world, instanceID);
		}
		statusMessage = "SubScene設定を適用できません";
		statusError = true;
		return false;
	}

	context.panelContext->host->RequestMarkSceneDirty();
	statusMessage = "SubScene設定を更新しました";
	statusError = false;
	return true;
}
