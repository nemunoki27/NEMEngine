#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSChangeTracker.h>
#include <Engine/Core/Assets/AssetTypes.h>

#include <unordered_map>

namespace Engine {

	struct EditorContext;
	class EditorSceneDirtyState;

	//============================================================================
	//	EditorSceneEditScope class
	//	一操作で変更したScene Instanceを記録する
	//============================================================================
	class EditorSceneEditScope {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorSceneEditScope(const EditorContext& context, EditorSceneDirtyState& dirtyState);
		~EditorSceneEditScope();
		EditorSceneEditScope(const EditorSceneEditScope&) = delete;
		EditorSceneEditScope& operator=(const EditorSceneEditScope&) = delete;

		// 操作が成功した場合だけ未保存状態を更新する
		void Commit();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const EditorContext& context_;
		EditorSceneDirtyState& dirtyState_;
		uint64_t listenerID_ = 0;
		std::unordered_map<UUID, AssetID> changedScenes_;

		//--------- functions ----------------------------------------------------

		static void OnMutation(ECSWorld& world, const Entity& entity, uint32_t typeID,
			ComponentMutationKind kind, void* userData);
	};
}
