#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

namespace Engine {

	//============================================================================
	//	CameraControllerSystem class
	//	カメラの追従、注視、揺れをTransformへ反映するシステム
	//============================================================================
	class CameraControllerSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraControllerSystem() = default;
		~CameraControllerSystem() override = default;

		// Play中のCameraControllerを更新する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CameraControllerSystem"; }
	};
} // Engine
