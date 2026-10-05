#include "ProfileInputSnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <filesystem>
#include <map>
#include <span>
#include <unordered_map>

namespace {

	// 保存順序に依存せずJSONの内容を識別する
	std::string HashProfileInput(const nlohmann::json& input) {

		const std::string text = input.dump();
		return Engine::ContentHash::SHA256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
	}

	// 付随BufferやShaderのincludeも内容の比較へ含める
	bool CollectProfileFiles(const std::filesystem::path& root, const std::string& prefix,
		std::map<std::string, std::filesystem::path>& files) {

		std::error_code error;
		if (!std::filesystem::is_directory(root, error) || error) { return false; }
		for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
			iterator != end && !error; iterator.increment(error)) {
			const auto status = iterator->symlink_status(error);
			if (error) { return false; }
			if (std::filesystem::is_symlink(status)) { return false; }
			if (std::filesystem::is_directory(status)) {
				if (iterator->path().filename() == ".git") { iterator.disable_recursion_pending(); }
				continue;
			}
			if (!std::filesystem::is_regular_file(status)) { continue; }
			const auto relative = iterator->path().lexically_relative(root);
			files.emplace(prefix + "/" + Engine::Algorithm::PathToUTF8(relative), iterator->path());
		}
		return !error;
	}
}

Engine::ProfileInputSnapshot Engine::ProfileInputSnapshotBuilder::Capture(const AssetDatabase& database,
	const ECSWorld& world, const SceneInstanceManager* scenes) {

	ProfileInputSnapshot snapshot;
	snapshot.assetStructureRevision = database.GetStructureRevision();
	snapshot.assetContentRevision = database.GetContentRevision();
	std::map<std::string, std::filesystem::path> files;
	bool complete = CollectProfileFiles(RuntimePaths::GetGameAssetsRoot(), "game", files);
	complete &= CollectProfileFiles(RuntimePaths::GetEngineAssetsRoot(), "engine", files);
	for (const auto& package : RuntimePaths::GetPackages()) {
		complete &= CollectProfileFiles(package.root, "package/" + package.name, files);
	}
	nlohmann::json assetInputs = nlohmann::json::array();
	// 読込中に変わったファイルは比較可能な入力と扱わない
	for (const auto& [name, path] : files) {
		std::error_code error;
		const auto beforeTime = std::filesystem::last_write_time(path, error);
		if (error) { complete = false; continue; }
		const auto beforeSize = std::filesystem::file_size(path, error);
		if (error) { complete = false; continue; }
		const std::string hash = ContentHash::FileSHA256(path);
		const auto afterTime = std::filesystem::last_write_time(path, error);
		if (error || hash.empty() || beforeTime != afterTime) { complete = false; continue; }
		const auto afterSize = std::filesystem::file_size(path, error);
		if (error || beforeSize != afterSize) { complete = false; continue; }
		assetInputs.push_back({ { "path", name }, { "size", beforeSize }, { "sha256", hash } });
	}
	snapshot.fileCount = assetInputs.size();
	// メモリ上で確定しているImport設定も内容へ含める
	std::map<std::string, nlohmann::json> metadata;
	for (const auto& [id, meta] : database.GetAssets()) {
		metadata.emplace(ToString(id), nlohmann::json{
			{ "path", meta.assetPath }, { "type", static_cast<uint32_t>(meta.type) },
			{ "importer", meta.importer }, { "importerVersion", meta.importerVersion }, { "settings", meta.importerSettings } });
	}
	snapshot.assetSHA256 = HashProfileInput({ { "files", assetInputs }, { "metadata", metadata } });
	const ProfileInputSnapshot worldSnapshot = CaptureWorld(world, scenes);
	snapshot.worldSHA256 = worldSnapshot.worldSHA256;
	snapshot.entityCount = worldSnapshot.entityCount;
	snapshot.complete = complete && worldSnapshot.complete && HasSameAssetRevisions(database, snapshot);
	return snapshot;
}

Engine::ProfileInputSnapshot Engine::ProfileInputSnapshotBuilder::CaptureWorld(const ECSWorld& world,
	const SceneInstanceManager* scenes) {

	ProfileInputSnapshot snapshot;
	bool complete = true;

	// 実行ごとに採番されるSceneのIDは読込順へ置き換える
	std::unordered_map<UUID, size_t> sceneOrder;
	nlohmann::json headers = nlohmann::json::array();
	if (scenes) {
		for (const auto& scene : scenes->GetAll()) {
			sceneOrder.emplace(scene.instanceID, headers.size());
			headers.push_back({ { "asset", ToString(scene.sceneAsset) }, { "header", ToJson(scene.header) } });
		}
	}
	const uint64_t worldRevision = world.GetDataRevision();
	std::map<std::string, nlohmann::json> entities;
	// 保存用の整形やScript callbackを呼ばず現在値だけを取得する
	world.ForEachAliveEntity([&](Entity entity) {
		nlohmann::json components;
		world.SerializeEntityComponents(entity, components);
		// 自動Seedで再生したEffectの条件も区別する
		if (const auto* runtime = TryGetParticleSystemRuntime(world, entity); runtime && runtime->effect.randomInitialized) {
			components["ProfileParticleSeed"] = runtime->effect.randomSeed;
		}
		const auto* object = world.TryGetComponent<SceneObjectComponent>(entity);
		const auto order = object ? sceneOrder.find(object->sceneInstanceID) : sceneOrder.end();
		const std::string key = (order != sceneOrder.end() ? std::to_string(order->second) : "standalone") + "/" +
			(object ? ToString(object->localFileID) : ToString(world.GetUUID(entity)));
		if (!entities.emplace(key, std::move(components)).second) { complete = false; }
	});
	snapshot.entityCount = entities.size();
	snapshot.worldSHA256 = HashProfileInput({ { "scenes", headers }, { "entities", entities } });
	snapshot.complete = complete && worldRevision == world.GetDataRevision();
	return snapshot;
}

bool Engine::ProfileInputSnapshotBuilder::HasSameAssetRevisions(const AssetDatabase& database,
	const ProfileInputSnapshot& snapshot) {

	return database.GetStructureRevision() == snapshot.assetStructureRevision &&
		database.GetContentRevision() == snapshot.assetContentRevision;
}
