#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <span>

namespace Engine {

	class ECSWorld;

	namespace HierarchyUtility {

		// 生存している親を取得する
		Entity GetParent(const ECSWorld& world, Entity entity);
		// 親子関係の循環を確認する
		bool CanSetParent(const ECSWorld& world, Entity child, Entity parent);

		// 保存されている兄弟順に合わせて親の子リンクを並べ直す
		void SortChildLinksBySiblingOrder(ECSWorld& world, Entity parent);

		// 指定したエンティティが親なしのルートか判定する
		bool IsRoot(ECSWorld& world, Entity entity);

		// 通常の子とジョイント接続された子を含むサブツリーを収集する
		std::vector<Entity> CollectLogicalSubtree(ECSWorld& world, Entity root);
		// 選択内の祖先に含まれるEntityと重複を除く
		std::vector<Entity> CollectLogicalRoots(ECSWorld& world, std::span<const Entity> selection);

	} // HierarchyUtility
} // Engine
