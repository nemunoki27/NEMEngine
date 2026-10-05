#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	struct EditorContext;
	class ECSWorld;

	namespace EditorHierarchyPolicy {

		// Scene所属とPrefab制約と循環を確認する
		bool CanSetParent(const EditorContext* context, ECSWorld& world, Entity child, Entity parent);
		// 同じSceneと親の兄弟順を変更できるか確認する
		bool CanReorder(const EditorContext* context, ECSWorld& world, Entity child, Entity anchor);
	} // EditorHierarchyPolicy
} // Engine
