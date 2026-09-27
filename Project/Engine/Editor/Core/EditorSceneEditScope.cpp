#include "EditorSceneEditScope.h"

//============================================================================
//	include
//============================================================================
#include "EditorContext.h"
#include "EditorSceneDirtyState.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

//============================================================================
//	EditorSceneEditScope classMethods
//============================================================================
Engine::EditorSceneEditScope::EditorSceneEditScope(const EditorContext& context, EditorSceneDirtyState& dirtyState) :
	context_(context), dirtyState_(dirtyState) {

	if (!context_.isPlaying && !context_.isPrefabEditing && context_.activeWorld) {
		listenerID_ = context_.activeWorld->AddComponentMutationListener(&OnMutation, this);
	}
}

Engine::EditorSceneEditScope::~EditorSceneEditScope() {

	if (listenerID_ != 0) {
		context_.activeWorld->RemoveComponentMutationListener(listenerID_);
	}
}

void Engine::EditorSceneEditScope::Commit() {

	if (listenerID_ == 0) {
		return;
	}
	// Header編集はEntity通知を持たないためActiveへ記録する
	if (changedScenes_.empty() && context_.activeSceneAsset) {
		changedScenes_.emplace(context_.activeSceneInstanceID, context_.activeSceneAsset);
	}
	for (const auto& [instance, asset] : changedScenes_) {
		dirtyState_.MarkDirty(asset, instance);
	}
}

void Engine::EditorSceneEditScope::OnMutation(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] uint32_t typeID, [[maybe_unused]] ComponentMutationKind kind, void* userData) {

	auto& scope = *static_cast<EditorSceneEditScope*>(userData);
	const auto* membership = world.TryGetComponent<SceneObjectComponent>(entity);
	if (!membership || !scope.context_.sceneInstances) {
		return;
	}
	// Prefabの参照元Assetと所属Sceneを混同しない
	const auto* scene = scope.context_.sceneInstances->Find(membership->sceneInstanceID);
	if (scene && scene->sceneAsset && !scene->persistent) {
		scope.changedScenes_[scene->instanceID] = scene->sceneAsset;
	}
}
