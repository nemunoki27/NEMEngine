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
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
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

	try {
		const bool trackChanges = world.GetKind() == ECSWorldKind::Authoring;
		const std::string readRevision = trackChanges ? storage_->CaptureRevision(scenePath, sourceAsset) : std::string{};
		// ファイルの読込失敗を実体生成へ持ち込まない
		nlohmann::json root;
		std::string diagnostic;
		if (!JsonFile::TryLoad(scenePath, root, &diagnostic) || !ValidateSceneFileRoot(root)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] Scene文書を読み込めません scene={} 詳細={}", Algorithm::PathToUTF8(scenePath), diagnostic);
			return false;
		}
		if (root.contains("ExternalActors") && !LoadExternalActors(scenePath, sourceAsset, root)) {
			return false;
		}
		SceneHeader header;
		// 出力先がなくてもHeaderの不正を検出する
		if (!FromJson(root["Header"], header, assetDatabase)) {
			return false;
		}
		if (outHeader) {
			if (const auto name = MakeSceneAssetName(scenePath); !name.empty()) {
				header.name = name;
			}
			header.guid = sourceAsset;
			EnsureSceneRenderFeatureProfile(header, Algorithm::PathToUTF8(scenePath), assetDatabase);
		}
		// 別の保存内容が混ざった読込では実体を作らない
		if (trackChanges && !storage_->MatchesRevision(scenePath, sourceAsset, readRevision)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] Scene読込中に外部変更を検出しました scene={}", Algorithm::PathToUTF8(scenePath));
			return false;
		}
		if (!LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities)) {
			return false;
		}
		// 成功した読込の基準だけを保存管理へ渡す
		if (trackChanges) storage_->TrackLoaded(scenePath, sourceAsset, readRevision);
		// 実体生成と一組でHeaderを公開する
		if (outHeader) {
			*outHeader = std::move(header);
		}
		return true;
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] Scene読込に失敗しました scene={} 詳細={}", Algorithm::PathToUTF8(scenePath), error.what());
		return false;
	}
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
