#include "BakedLightingInput.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <cmath>
#include <unordered_map>
#include <stdexcept>

//============================================================================
//	BakedLightingInputBuilder validationMethods
//============================================================================
bool Engine::BakedLightingInputBuilder::IsCurrent(const BakedLightingData& data, ECSWorld& world, UUID sceneInstance) {

	if (LightingState(world, data.sceneAsset, sceneInstance) != data.inputRevisions.at("lightingState")) return false;
	std::unordered_map<UUID, Entity> entities;
	world.ForEach<SceneObjectComponent>([&](Entity entity, const SceneObjectComponent& object) {
		if (object.sceneInstanceID == sceneInstance && object.activeInHierarchy) entities[object.localFileID] = entity;
	});
	// ベイク後に追加された固定背景も失効条件に含める
	size_t receiverCount = 0;
	world.ForEach<MeshRendererComponent, SceneObjectComponent>([&](Entity, const MeshRendererComponent& renderer,
		const SceneObjectComponent& object) {
		if (object.sceneInstanceID == sceneInstance && object.activeInHierarchy && renderer.visible && renderer.contributeBakedLighting) ++receiverCount;
	});
	if (receiverCount != data.receivers.size()) return false;
	for (const auto& receiver : data.receivers) {
		const auto found = entities.find(receiver.localFileID);
		if (found == entities.end()) return false;
		const auto* renderer = world.TryGetComponent<MeshRendererComponent>(found->second);
		const auto* transform = world.TryGetComponent<TransformComponent>(found->second);
		if (!renderer || !transform || ReceiverSettings(world, found->second) != receiver.settings) return false;
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {
				if (std::abs(transform->worldMatrix.m[row][column] - receiver.worldMatrix.m[row][column]) > 1e-5f) return false;
			}
		}
	}
	return true;
}

void Engine::BakedLightingInputBuilder::ValidateAssets(const BakedLightingData& data, const AssetDatabase& database,
	bool readSourceFiles) {

	for (const auto& asset : data.inputRevisions.at("assets")) {
		const AssetID id = ParseAssetID(asset, "assetId");
		const auto* metadata = database.Find(id);
		if (!metadata) throw std::runtime_error("ベイクの入力Assetが見つかりません");
		const auto matches = [&](const AssetMeta& value) {
			return value.guid == id && value.importer == asset.at("importer").get<std::string>() &&
				value.importerVersion == asset.at("importerVersion").get<uint32_t>() &&
				value.importerSettings == asset.at("importerSettings");
		};
		if (!matches(*metadata)) throw std::runtime_error("ベイク後にインポート設定が変更されています 再ベイクしてください");
		if (!readSourceFiles) continue;
		// Workerの索引Snapshotと保存直前のファイルを照合する
		const auto path = database.ResolveFullPath(id);
		AssetMeta source;
		if (!AssetMetaStorage::ReadMetaFile(AssetMetaStorage::MetaPathOf(path), source) || !matches(source) ||
			ContentHash::FileSHA256(path) != asset.at("hash").get<std::string>()) {
			throw std::runtime_error("ベイク後に入力Assetが変更されています 再ベイクしてください");
		}
	}
}
