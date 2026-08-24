#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	//============================================================================
	//	CollisionComponent structure
	//	Entityに衝突設定と1つの衝突形状を持たせるコンポーネント
	//============================================================================
	struct CollisionComponent {

		static constexpr bool kHasECSHooks = true;

		// Collision全体の有効状態
		bool enabled = true;
		// trueの場合は押し戻しで移動しない
		bool isStatic = false;
		// 押し戻しを受けるか
		bool enablePushback = true;
		// CollisionManagerで作成したCollisionタイプのビットマスク
		uint32_t typeMask = 1u;
		// 判定に使用する単一形状
		CollisionShape shape{};

		// Registryから呼ばれるワールド依存Storageフック
		static void OnAdded(
			ECSWorld& world, const Entity& entity, CollisionComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, CollisionComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, CollisionComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, CollisionComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const CollisionComponent& component, nlohmann::json& out);
	};

	// CollisionSystemだけが更新する非保存状態
	struct CollisionRuntimeStateComponent {

		static constexpr bool kSerializable = false;

		bool colliding = false;
	};

	//============================================================================
	//	CollisionCompoundComponent structure
	//	ComponentManifestの固定IDを維持する予約コンポーネント
	//============================================================================
	struct CollisionCompoundComponent {

		static constexpr ComponentWorldDomain kWorldDomain =
			ComponentWorldDomain::Runtime;
		static constexpr bool kSerializable = false;
	};

	// 実行時の衝突状態を返す
	bool IsCollisionColliding(const ECSWorld& world, const Entity& entity);

	// jsonからコンポーネントへ変換する
	void DeserializeComponent(ECSWorld& world, const Entity& entity,
		const nlohmann::json& in, CollisionComponent& component);
	// コンポーネントからjsonへ変換する
	void SerializeComponent(const ECSWorld& world, const Entity& entity,
		const CollisionComponent& component, nlohmann::json& out);
	// チャンク内データだけをjsonへ変換する
	void SerializeComponentDraft(const CollisionComponent& component, nlohmann::json& out);

} // Engine
