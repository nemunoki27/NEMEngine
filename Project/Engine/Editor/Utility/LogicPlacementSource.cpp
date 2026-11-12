#include "LogicPlacementSource.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseCache.h>

#include <exception>

using namespace Engine;

bool Engine::ResolveLogicPlacementSource(ECSWorld& world, AssetDatabase* database, const LogicPlacementSource& source,
	bool is2D, TransformComponent& transform, std::string& diagnostic, PrefabBaseCache* cache) try {

	diagnostic.clear();
	if (source.object && source.prefab) {
		diagnostic = "配置元はオブジェクトまたはPrefabのどちらかを設定してください";
		return false;
	}
	if (source.object) {
		const auto object = world.FindByUUID(source.object);
		const auto* value = world.IsAlive(object) ? world.TryGetComponent<TransformComponent>(object) : nullptr;
		if (!value) {
			diagnostic = "配置元のオブジェクトまたはTransformが見つかりません";
			return false;
		}
		transform = *value;
	} else if (source.prefab && database) {
		const auto* meta = database->Find(source.prefab);
		if (!meta || meta->type != AssetType::Prefab) {
			diagnostic = "配置元のPrefabが見つかりません";
			return false;
		}
		PrefabBaseCache local;
		const auto base = (cache ? *cache : local).Load(*database, source.prefab);
		const PrefabBaseEntity* root = nullptr;
		if (base) for (const auto& [id, object] : *base) if (object.isRoot) { root = &object; break; }
		if (!root || !root->components.contains("Transform")) {
			diagnostic = "PrefabのルートまたはTransformを読み込めません";
			return false;
		}
		transform = root->components.at("Transform").get<TransformComponent>();
	} else {
		diagnostic = "配置オブジェクトを設定してください";
		return false;
	}
	if ((transform.dimension == Dimension::Type2D) != is2D) {
		diagnostic = "配置元の次元が一致しません。選択した次元に合うオブジェクトを設定してください";
		return false;
	}
	return true;
} catch (const std::exception&) {

	diagnostic = "配置元の設定を読み込めません";
	return false;
}
