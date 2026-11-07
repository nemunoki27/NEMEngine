#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	DayNightCycleSystem class
	//	保存値を変更せず太陽と月の描画方向を更新する
	//============================================================================
	class DayNightCycleSystem : public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		static void RestoreEditPreviews(ECSWorld& world);
		const char* GetName() const override { return "DayNightCycleSystem"; }
	};

	bool ResolveDayNightLightDirection(ECSWorld& world, Entity light, Vector3& direction);
	bool ResolveDayNightAtmosphereLights(ECSWorld& world, Entity atmosphere, Entity& sun, Entity& moon);
}
