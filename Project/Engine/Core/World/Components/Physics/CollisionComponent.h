#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	CollisionComponent structure
	//	Entityに衝突タイプと複数の衝突形状を持たせるコンポーネント
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

	// CollisionSystemだけが更新する実行時状態
	struct CollisionRuntimeStateComponent {

		static constexpr ComponentWorldDomain kWorldDomain =
			ComponentWorldDomain::Runtime;
		static constexpr bool kSerializable = false;

		bool colliding = false;
	};

	//============================================================================
	//	CollisionCompound structures
	//	Runtime判定で共有する変更不可の形状列
	//============================================================================
	struct CollisionCompoundBlob {

		BlobArray<CollisionShape> shapes{};
	};

	struct CollisionCompoundComponent {

		static constexpr ComponentWorldDomain kWorldDomain =
			ComponentWorldDomain::Runtime;
		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		BlobAssetReference<CollisionCompoundBlob> blob{};

		// Registryから呼ばれるBlob参照のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, CollisionCompoundComponent& component);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, CollisionCompoundComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, CollisionCompoundComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, CollisionCompoundComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const CollisionCompoundComponent& component, nlohmann::json& out);
	};

	// 衝突形状一覧を返す
	std::span<CollisionShape> GetCollisionShapes(
		ECSWorld& world, const Entity& entity);
	std::span<const CollisionShape> GetCollisionShapes(
		const ECSWorld& world, const Entity& entity);
	// 衝突形状一覧を置き換える
	void SetCollisionShapes(ECSWorld& world, const Entity& entity,
		std::span<const CollisionShape> shapes);
	// 衝突形状が無ければ既定形状を作る
	void EnsureCollisionShapes(ECSWorld& world, const Entity& entity);
	// 指定位置の衝突形状を返す
	CollisionShape* TryGetCollisionShape(
		ECSWorld& world, const Entity& entity, uint32_t index);
	const CollisionShape* TryGetCollisionShape(
		const ECSWorld& world, const Entity& entity, uint32_t index);
	// 衝突形状を追加する
	void AddCollisionShape(ECSWorld& world, const Entity& entity,
		const CollisionShape& shape = CollisionShape{});
	// 指定位置の衝突形状を削除する
	bool RemoveCollisionShape(ECSWorld& world, const Entity& entity, uint32_t index);
	// 衝突形状をすべて削除する
	void ClearCollisionShapes(ECSWorld& world, const Entity& entity);
	// Runtime判定用の変更不可形状列を再構築する
	void RebuildCollisionCompound(ECSWorld& world, const Entity& entity);
	// 実行時の衝突状態を返す
	bool IsCollisionColliding(const ECSWorld& world, const Entity& entity);

	// jsonからコンポーネントへ変換する
	void DeserializeComponent(ECSWorld& world, const Entity& entity,
		const nlohmann::json& in, CollisionComponent& component);
	// コンポーネントからjsonへ変換する
	void SerializeComponent(const ECSWorld& world, const Entity& entity,
		const CollisionComponent& component, nlohmann::json& out);
	// 指定形状を含めてjsonへ変換する
	void SerializeCollisionDraft(const CollisionComponent& component,
		std::span<const CollisionShape> shapes, nlohmann::json& out);
	// チャンク内データだけをjsonへ変換する
	void SerializeComponentDraft(const CollisionComponent& component, nlohmann::json& out);

} // Engine
