#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;

	namespace HierarchyUtility {

		// 保存されている兄弟順に合わせて親の子リンクを並べ直す
		void SortChildLinksBySiblingOrder(ECSWorld& world, Entity parent);

		// 指定したエンティティがルート（親なし）か判定
		bool IsRoot(ECSWorld& world, Entity entity);

	} // HierarchyUtility
} // Engine
