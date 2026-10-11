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
#include <algorithm>
#include <utility>
#include <vector>
#include <functional>
#include <unordered_set>

using namespace Engine::SceneDocument;

//============================================================================
//	SceneSystem classMethods
//============================================================================
bool Engine::SceneSystem::CopySceneAssets(
	const std::vector<SceneAssetCopy>& copies, std::string& error, std::shared_ptr<SceneAssetStorage> storage) {

	return SceneAssetCopier::CopySceneAssets(copies, error, std::move(storage));
}

bool Engine::SceneSystem::LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, SceneHeader* outHeader, std::vector<Entity>* outCreatedEntities) const {

	const auto lifetime = world.GetLifetime();
	try {
		const bool trackChanges = world.GetKind() == ECSWorldKind::Authoring;
		// ファイルの読込失敗を実体生成へ持ち込まない
		const auto document = ReadSceneDocument(scenePath, sourceAsset);
		if (!document) return false;
		const auto& root = document->root;
		const auto& readRevision = document->revision;
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
		}
		// 別の保存内容が混ざった読込では実体を作らない
		if (trackChanges && !storage_->MatchesRevision(scenePath, sourceAsset, readRevision)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] Scene読込中に外部変更を検出しました scene={}",
				Algorithm::PathToUTF8(scenePath));
			return false;
		}
		if (!LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities)) {
			return false;
		}
		// 成功した読込の基準だけを保存管理へ渡す
		if (trackChanges) {
			storage_->TrackLoaded(scenePath, sourceAsset, readRevision);
		}
		// 実体生成と一組でHeaderを公開する
		if (outHeader) {
			*outHeader = std::move(header);
		}
		return true;
	} catch (const std::exception& error) {
		// Scene管理側へ終了したWorldを返さない
		if (!lifetime->IsAlive()) {
			throw;
		}
		Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] Scene読込に失敗しました scene={} 詳細={}",
			Algorithm::PathToUTF8(scenePath), error.what());
		return false;
	}
}

bool Engine::SceneSystem::SaveScene(const std::filesystem::path& scenePath, ECSWorld& world, const SceneHeader& header,
	AssetDatabase& database, const std::vector<Entity>* entitiesSubset) const {

	SceneSaveSnapshot snapshot{};
	if (!CaptureSaveSnapshot(scenePath, world, header, database, snapshot, entitiesSubset)) {
		return false;
	}
	return WriteSaveSnapshot(std::move(snapshot));
}

bool Engine::SceneSystem::CaptureSaveSnapshot(const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database, SceneSaveSnapshot& outSnapshot,
	const std::vector<Entity>* entitiesSubset) const {

	if (!SceneSnapshotBuilder::CaptureSaveSnapshot(scenePath, world, header, database, outSnapshot, entitiesSubset)) {
		return false;
	}
	outSnapshot.storage = storage_;
	return true;
}

bool Engine::SceneSystem::WriteSaveSnapshot(SceneSaveSnapshot snapshot) {

	if (snapshot.scenePath.empty() || !snapshot.root.is_object()) {
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

bool Engine::SceneSystem::LoadFromJson(const nlohmann::json& sourceRoot, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, std::vector<Entity>* outCreatedEntities) const {

	return SceneInstantiator::LoadFromJson(sourceRoot, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities);
}

Engine::SceneSystem::SceneSystem() : storage_(std::make_shared<SceneAssetStorage>()) {
}

Engine::SceneSystem::SceneSystem(std::shared_ptr<SceneAssetStorage> storage) : storage_(std::move(storage)) {

	if (!storage_) {
		storage_ = std::make_shared<SceneAssetStorage>();
	}
}

std::shared_ptr<const Engine::SceneSystem::PreparedDocument> Engine::SceneSystem::ReadSceneDocument(
	const std::filesystem::path& path, AssetID asset) const {

	const std::string revision = storage_->CaptureRevision(path, asset);
	const std::string key = Algorithm::PathToUTF8(path.lexically_normal()) + ":" + ToString(asset);
	{
		std::scoped_lock lock(documentCache_->mutex);
		const auto found = documentCache_->entries.find(key);
		if (found != documentCache_->entries.end() && found->second.document->revision == revision) {

			found->second.lastAccess = ++documentCache_->accessSerial;
			return found->second.document;
		}
	}
	auto document = std::make_shared<PreparedDocument>();
	std::string diagnostic;
	if (!JsonFile::TryLoad(path, document->root, &diagnostic) || !ValidateSceneFileRoot(document->root)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] Scene文書を読み込めません scene={} 詳細={}",
			Algorithm::PathToUTF8(path), diagnostic);
		return {};
	}
	if (document->root.contains("ExternalActors") && !LoadExternalActors(path, asset, document->root)) return {};
	// 準備中に保存された文書は公開しない
	if (!storage_->MatchesRevision(path, asset, revision)) return {};
	document->revision = revision;
	std::scoped_lock lock(documentCache_->mutex);
	// 古い文書だけを外して切替先の解析結果を残す
	if (documentCache_->entries.size() >= 16u && !documentCache_->entries.contains(key)) {

		const auto oldest = std::min_element(documentCache_->entries.begin(), documentCache_->entries.end(),
			[](const auto& left, const auto& right) { return left.second.lastAccess < right.second.lastAccess; });
		documentCache_->entries.erase(oldest);
	}
	documentCache_->entries.insert_or_assign(key, CachedDocument{ document, ++documentCache_->accessSerial });
	return document;
}

bool Engine::SceneSystem::PrepareSceneDocuments(AssetID rootAsset,
	const std::unordered_map<AssetID, std::filesystem::path>& paths,
	const std::function<bool(const nlohmann::json&)>& prepareAssets) const {

	std::unordered_set<AssetID> ancestors;
	std::unordered_set<AssetID> prepared;
	const auto prepare = [&](auto&& self, AssetID asset) -> bool {

		if (ancestors.contains(asset)) return false;
		if (prepared.contains(asset)) return true;
		const auto path = paths.find(asset);
		if (path == paths.end()) return false;
		ancestors.insert(asset);
		const auto document = ReadSceneDocument(path->second, asset);
		SceneHeader header;
		if (!document || !FromJson(document->root.at("Header"), header, nullptr)) return false;
		// 確定した文書からCPU側のAsset準備を進める
		if (prepareAssets && !prepareAssets(document->root)) return false;
		// Workerには確定済みのAsset配置だけを渡す
		for (const auto& slot : header.subScenes) {
			if (slot.enabled && slot.sceneAsset && !self(self, slot.sceneAsset)) return false;
		}
		ancestors.erase(asset);
		prepared.insert(asset);
		return true;
	};
	return prepare(prepare, rootAsset);
}
