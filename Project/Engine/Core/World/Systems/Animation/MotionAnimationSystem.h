#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>

namespace Engine {

	// 開始姿勢を基準とした移動と回転を評価する
	class MotionAnimationSystem : public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		static void RestoreEditPreviews(ECSWorld& world);
		const char* GetName() const override { return "MotionAnimationSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// 再生操作と対象追跡を反映して姿勢を更新する
		void UpdateMotion(ECSWorld& world, Entity entity, MotionAnimationRecord& record, SystemContext& context);
	};
	void RestoreMotionAnimationPreview(ECSWorld& world, Entity entity, MotionAnimationRuntime& runtime);
}
