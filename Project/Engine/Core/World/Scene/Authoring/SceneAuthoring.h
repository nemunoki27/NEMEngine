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
	Entity CreateGameObject(ECSWorld& world, const std::string_view& name = "Entity");
	// 追加コンポーネントを含む最終アーキタイプへゲームオブジェクトを作成する
	Entity CreateGameObject(ECSWorld& world, const std::string_view& name,
		std::span<const uint32_t> additionalTypeIDs);
	// ゲームオブジェクトのデフォルトコンポーネントを追加する
	void EnsureGameObjectDefaults(ECSWorld& world, const Entity& entity, const std::string_view& defaultName = "Entity");

	// 名前を base_N 形式として解析する、解析できなければfalse
	bool TryParseIndexedName(const std::string& name, std::string& outBase, uint32_t& outIndex);
	// シーン内で重複しないエンティティ名を返す、未使用ならそのまま、衝突時は base_N にする、excludeは判定対象から外す
	std::string MakeUniqueEntityName(ECSWorld& world, const std::string_view& desiredName,
		const Entity& exclude = Entity::Null());
} // Engine
