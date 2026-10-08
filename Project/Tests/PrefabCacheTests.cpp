#include "PrefabCacheTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseCache.h>

// c++
#include <filesystem>
#include <memory>

bool TestPrefabCacheLifetime(Engine::AssetDatabase& database, Engine::AssetID asset) {

	Engine::PrefabBaseCache cache;
	const auto originalBase = cache.Load(database, asset);
	if (!originalBase) return false;
	bool passed = true;
	const auto file = database.ResolveFullPath(asset);
	const auto originalJson = Engine::JsonAdapter::Load(file);
	auto changedJson = originalJson;
	auto& name = changedJson["Entities"][0]["Components"]["Name"]["name"];
	const std::string oldName = name.get<std::string>();
	if (oldName.empty()) return false;

	// 同じ時刻とサイズの保存も通知で再読込する
	std::string changedName = oldName;
	changedName[0] = oldName[0] == 'X' ? 'Y' : 'X';
	name = changedName;
	const auto originalTime = std::filesystem::last_write_time(file);
	const auto originalSize = std::filesystem::file_size(file);
	passed &= Engine::JsonAdapter::SaveCanonical(file, changedJson);
	std::filesystem::last_write_time(file, originalTime);
	passed &= std::filesystem::file_size(file) == originalSize;
	database.NotifyContentChanged(asset);
	const auto changedBase = cache.Load(database, asset);
	passed &= changedBase && changedBase != originalBase;
	const auto changedID = Engine::FromString16Hex(changedJson["Entities"][0]["LocalFileID"].get<std::string>());
	if (changedBase) passed &= changedBase->at(changedID).components["Name"]["name"] == changedName;
	passed &= originalBase->at(changedID).components["Name"]["name"] == oldName;

	// 破損した文書を旧基準で隠さず、取得済みSnapshotは保持する
	changedJson["SchemaVersion"] = "invalid";
	passed &= Engine::JsonAdapter::SaveCanonical(file, changedJson);
	database.NotifyContentChanged(asset);
	passed &= !cache.Load(database, asset);
	passed &= originalBase->at(changedID).components["Name"]["name"] == oldName;
	passed &= Engine::JsonAdapter::SaveCanonical(file, originalJson);
	database.NotifyContentChanged(asset);
	passed &= static_cast<bool>(cache.Load(database, asset));

	// 所有元の終了後も利用中の基準を保持する
	std::shared_ptr<const Engine::PrefabBaseEntities> retained;
	std::weak_ptr<const Engine::PrefabBaseEntities> released;
	{
		Engine::PrefabBaseCache scopedCache;
		retained = scopedCache.Load(database, asset);
		released = retained;
	}
	passed &= retained && !released.expired();
	retained.reset();
	passed &= released.expired();
	return passed;
}
