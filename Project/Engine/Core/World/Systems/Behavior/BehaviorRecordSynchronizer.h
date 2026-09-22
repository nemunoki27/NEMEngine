#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/World/BehaviorWorld.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	struct ScriptEntry;

	//============================================================================
	//	BehaviorRecordSynchronizer class
	//	Script設定と実行recordを対応付ける
	//============================================================================
	class BehaviorRecordSynchronizer {
	public:
		// 保存GUIDから登録型を解決する
		static bool TryResolveTypeID(ScriptEntry& entry, uint32_t& outTypeID);
		BehaviorRecordSynchronizer(BehaviorWorld& runtime, std::vector<Entity>& dirtyEntities,
			bool& participantsDirty, bool& enableTransitionsDirty);

		// Scriptの登録状態を同期する
		void SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep);
		// Scriptの登録状態を同期する
		void SynchronizeDirtyRecords(ECSWorld& world, SystemContext& context);
		// Scriptの登録状態を同期する
		void SynchronizeEntityRecords(ECSWorld& world, SystemContext& context,
			const Entity& entity, bool clearOwnerSeen);
		// Scriptの登録状態を同期する
		void QueueScriptEntity(const Entity& entity);
	private:
		//--------- variables ----------------------------------------------------

		BehaviorWorld& runtime_;
		std::vector<Entity>& dirtyScriptEntities_;
		bool& participantsDirty_;
		bool& enableTransitionsDirty_;
	};
}
