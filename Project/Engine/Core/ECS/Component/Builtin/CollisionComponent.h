#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionTypes.h>
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>

namespace Engine {

	//============================================================================
	//	CollisionComponent structure
	//	Entityに衝突タイプと複数の衝突形状を持たせるコンポーネント
	//============================================================================
	struct CollisionComponent {

		// Collision全体の有効状態
		bool enabled = true;
		// trueの場合は押し戻しで移動しない
		bool isStatic = false;
		// 押し戻しを受けるか
		bool enablePushback = true;
		// CollisionManagerで作成したCollisionタイプのビットマスク
		uint32_t typeMask = 1u;

		// Entityに設定された衝突形状一覧
		std::vector<CollisionShape> shapes = { CollisionShape{} };
	};

	// jsonからコンポーネントへ変換する
	void from_json(const nlohmann::json& in, CollisionComponent& component);
	// コンポーネントからjsonへ変換する
	void to_json(nlohmann::json& out, const CollisionComponent& component);

	ENGINE_REGISTER_COMPONENT(CollisionComponent, "Collision");
} // Engine
