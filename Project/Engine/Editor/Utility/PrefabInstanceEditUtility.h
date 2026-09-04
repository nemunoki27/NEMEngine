#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	// front
	class ECSWorld;
	struct EditorContext;

	//============================================================================
	//	PrefabInstanceEditUtility namespace
	//	Prefabインスタンスの編集可否を判定する
	//============================================================================
	namespace PrefabInstanceEditUtility {

		// Prefab由来のEntityか
		bool IsPrefabEntity(ECSWorld& world, const Entity& entity);
		// Prefabインスタンスのルートか
		bool IsPrefabRoot(ECSWorld& world, const Entity& entity);
		// Prefabインスタンスの階層内か
		bool IsInPrefabInstance(ECSWorld& world, const Entity& entity);
		// Prefabインスタンスのリンクを解除できるか
		bool CanUnpack(const EditorContext* editorContext, ECSWorld& world, const Entity& entity);
		// Entityを削除できるか
		bool CanDelete(const EditorContext* editorContext, ECSWorld& world, const Entity& entity);
		// 親を変更できるか
		bool CanChangeParent(const EditorContext* editorContext, ECSWorld& world,
			const Entity& entity, const Entity& newParent);
		// 兄弟順を変更できるか
		bool CanChangeSiblingOrder(const EditorContext* editorContext, ECSWorld& world,
			const Entity& entity, const Entity& anchor);
	}
} // Engine
