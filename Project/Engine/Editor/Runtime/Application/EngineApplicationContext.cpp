#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include "PrefabEditSession.h"
#include "EditorPlaySession.h"
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>

using namespace Engine;

//============================================================================
//	EngineApplication play methods
//============================================================================

void Engine::EngineApplication::RefreshActiveWorldContext() {

	if constexpr (BuildConfig::kEditorEnabled) {
		std::vector<AssetID> protectedAssets;
		for (const auto& scene : editScenes_.GetAll()) protectedAssets.push_back(scene.sceneAsset);
		for (const auto& scene : GetActiveScenes().GetAll()) protectedAssets.push_back(scene.sceneAsset);
		sceneSystem_.GetStorage()->SetProtectedScenes(protectedAssets);
	}

	ECSWorld* world = GetActiveWorld();
	const SceneHeader* header = GetActiveSceneHeader();
	SceneInstanceManager& activeScenes = GetActiveScenes();
	const SceneInstance* activeSceneInstance = activeScenes.GetActive();
	if (activeSceneInstance) {
		if (const AssetMeta* meta =
			assetDatabase_.Find(activeSceneInstance->sceneAsset)) {
			activeScenePath_ = meta->assetPath;
		}
	}

	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	systemContext_.world = world;

	if (world) {

		WorldCommandServices services{};
		services.assetDatabase = &assetDatabase_;
		services.sceneInstances = &activeScenes;
		services.sceneSystem = &sceneSystem_;
		world->SetCommandServices(services);
	}

	systemContext_.activeSceneHeader = header;
	CollisionSettings::GetInstance().BindGlobal();

	if constexpr (BuildConfig::kEditorEnabled) {

		editorContext_.isPlaying = worldManager_.IsPlaying();
		editorContext_.isPlayPaused = playSession_->IsPaused();
		editorContext_.activeScenePath = activeScenePath_;
		editorContext_.activeSceneHeader = header;
		editorContext_.activeSceneAsset = activeSceneInstance ? activeSceneInstance->sceneAsset : activeScene_;
		editorContext_.activeSceneInstanceID = activeSceneInstance ? activeSceneInstance->instanceID : UUID{};
		editorContext_.activeSceneDirty =
			editorManager_.IsSceneDirty(editorContext_.activeSceneAsset);
		editorContext_.sceneInstances = &activeScenes;
		editorContext_.activeWorld = world;
		editorContext_.editWorld = &worldManager_.GetEditWorld();
		editorContext_.assetDatabase = &assetDatabase_;
		editorContext_.sceneStorage = sceneSystem_.GetStorage();
		editorContext_.scriptBuildService = &scriptBuildService_;

		prefabSession_->ApplyEditorContext(editorContext_);
	}
}

const Engine::SceneHeader* Engine::EngineApplication::GetActiveSceneHeader() {

	// In-Context編集ではhostシーンを参照する
	const SceneInstance* instance = GetActiveScenes().GetActive();
	return instance ? &instance->header : nullptr;
}
