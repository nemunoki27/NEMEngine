#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SceneAuthoring namespace
//	シーンエディタ関連のコードをまとめるための名前空間
//============================================================================
namespace Engine::SceneAuthoring {

	// 新しいゲームオブジェクトを作成する
	Entity CreateGameObject(ECSWorld& world, std::string_view name = "Entity");
	// 追加コンポーネントを含む最終アーキタイプへゲームオブジェクトを作成する
	Entity CreateGameObject(
		ECSWorld& world, std::string_view name, std::span<const uint32_t> additionalTypeIDs, UUID stableUUID = UUID{});
	// ゲームオブジェクトのデフォルトコンポーネントを追加する
	void EnsureGameObjectDefaults(ECSWorld& world, Entity entity, std::string_view defaultName = "Entity");

	// 名前の末尾から連番を解析する
	bool TryParseIndexedName(const std::string& name, std::string& outBase, uint32_t& outIndex);
	// 指定Entityを除きWorld内で重複しない名前を返す
	std::string MakeUniqueEntityName(ECSWorld& world, std::string_view desiredName, Entity exclude = Entity::Null());
}
