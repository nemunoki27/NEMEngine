#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	// Timelineの論理時刻と物理評価をそろえる
	class TimelineSystem : public ISystem {
	public:

		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "TimelineSystem"; }
	};

	// Timelineの骨追従判定を物理判定の直前に確定する
	class TimelineAttachmentSystem : public ISystem {
	public:

		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "TimelineAttachmentSystem"; }
	};
}
