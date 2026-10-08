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

	//============================================================================
	//	BehaviorParticipantCache structure
	//	更新参加者と実行順のcache
	//============================================================================
	struct BehaviorParticipantCache {

		struct SyncParticipant {

			BehaviorHandle handle;
			Entity owner = Entity::Null();
			int32_t slot = 0;
			int32_t executionOrder = 0;
		};

		std::vector<SyncParticipant> participants_;
		bool participantsDirty_ = true;
		uint64_t executionOrderRevision_ = 0;
		std::vector<SyncParticipant> lateUpdateParticipants_;

		// 構造変更時に実行順を再構築する
		void RebuildParticipants(ECSWorld& world, BehaviorWorld& runtime);
	};
}
