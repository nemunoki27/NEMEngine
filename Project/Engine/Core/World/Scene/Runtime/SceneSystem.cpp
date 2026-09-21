#include "SceneSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>
#include <Engine/Core/World/Scene/Serialization/SceneSnapshotBuilder.h>
#include <Engine/Core/World/Scene/Serialization/SceneInstantiator.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetCopier.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <utility>
#include <vector>

using namespace Engine::SceneDocument;

//============================================================================
//	SceneSystem classMethods
//============================================================================
bool Engine::SceneSystem::CopySceneAssets(const std::vector<SceneAssetCopy>& copies, std::string& error,
	std::shared_ptr<SceneAssetStorage> storage) {

	return SceneAssetCopier::CopySceneAssets(copies, error, std::move(storage));
}

bool Engine::SceneSystem::LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, SceneHeader* outHeader, std::vector<Entity>* outCreatedEntities) const {

	// 読み込み開始時の状態を基準にし、途中の外部変更も保存時に検出する
	try {
		if (world.GetKind() == ECSWorldKind::Authoring) {
			storage_->TrackLoaded(scenePath, sourceAsset);
		}
	} catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] 保存状態を確認できません scene={} 詳細={}", Algorithm::PathToUTF8(scenePath), exception.what());
		return false;
	}
	// ファイルからnlohmann::jsonをロード
	nlohmann::json root = JsonAdapter::Load(scenePath, true);
	if (!ValidateSceneFileRoot(root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] 未対応のScene Schemaです 期待値={} scene={}",
			kSceneSchemaVersion, Algorithm::PathToUTF8(scenePath));
		return false;
	}
	if (root.contains("ExternalActors") &&
		!LoadExternalActors(scenePath, sourceAsset, root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] ExternalActorを読み込めません scene={}",
			Algorithm::PathToUTF8(scenePath));
		return false;
	}
	if (outHeader) {
		if (!FromJson(root["Header"], *outHeader, assetDatabase)) {
			return false;
		}
		// シーン表示名はファイル名を正として、外部リネーム後も古いHeader名を残さない
		if (const std::string assetName = MakeSceneAssetName(scenePath);
			!assetName.empty()) {
			outHeader->name = assetName;
		}
		outHeader->guid = sourceAsset;
		EnsureSceneRenderFeatureProfile(*outHeader,
			Algorithm::PathToUTF8(scenePath), assetDatabase);
	}
	if (LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities)) {
		return true;
	}

	// 読込途中のEntityを残さず、呼び出し元が同じWorldを継続利用できる状態へ戻す
	std::vector<Entity> failedEntities;
	world.ForEach<SceneObjectComponent>([&](const Entity& entity, SceneObjectComponent& sceneObject) {
		if (sceneObject.sceneInstanceID == sceneInstanceID) {
			failedEntities.emplace_back(entity);
		}
		});
	for (auto it = failedEntities.rbegin(); it != failedEntities.rend(); ++it) {
		world.DestroyEntity(*it);
	}
	world.FlushPendingDestroyEntities();
	if (outCreatedEntities) {
		outCreatedEntities->clear();
	}
	return false;
}

bool Engine::SceneSystem::SaveScene(const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database, const std::vector<Entity>* entitiesSubset) const {

	SceneSaveSnapshot snapshot{};
	if (!CaptureSaveSnapshot(scenePath, world, header,
		database, snapshot, entitiesSubset)) {
		return false;
	}
	return WriteSaveSnapshot(std::move(snapshot));
}

bool Engine::SceneSystem::CaptureSaveSnapshot(
	const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database,
	SceneSaveSnapshot& outSnapshot,
	const std::vector<Entity>* entitiesSubset) const {

	if (!SceneSnapshotBuilder::CaptureSaveSnapshot(scenePath, world, header, database, outSnapshot, entitiesSubset)) {
		return false;
	}
	outSnapshot.storage = storage_;
	return true;
}

bool Engine::SceneSystem::WriteSaveSnapshot(
	SceneSaveSnapshot snapshot) {

	if (snapshot.scenePath.empty() ||
		!snapshot.root.is_object()) {
		return false;
	}
	std::string error;
	const auto storage = snapshot.storage ? snapshot.storage : std::make_shared<SceneAssetStorage>();
	if (!storage->Save(std::move(snapshot), error)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] 保存を中断しました: {}", error);
		return false;
	}
	return true;
}

nlohmann::json Engine::SceneSystem::SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset) const {

	return SceneSnapshotBuilder::SerializeEntities(world, subset);
}

bool Engine::SceneSystem::LoadFromJson(const nlohmann::json& sourceRoot, ECSWorld& world,
	AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
	std::vector<Entity>* outCreatedEntities) const {

	return SceneInstantiator::LoadFromJson(sourceRoot, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities);
}

Engine::SceneSystem::SceneSystem() :
	storage_(std::make_shared<SceneAssetStorage>()) {
}

Engine::SceneSystem::SceneSystem(std::shared_ptr<SceneAssetStorage> storage) :
	storage_(std::move(storage)) {

	if (!storage_) {
		storage_ = std::make_shared<SceneAssetStorage>();
	}
}
