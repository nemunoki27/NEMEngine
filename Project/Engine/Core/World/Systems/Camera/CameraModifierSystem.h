#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	CameraModifierSystem class
	//	カメラモディファイア制御の更新を行うシステム
	//============================================================================
	class CameraModifierSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraModifierSystem() = default;
		~CameraModifierSystem() override = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CameraControllerSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------



		//--------- functions ----------------------------------------------------

	};
} // Engine