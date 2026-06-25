#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	CameraShakeSystem class
	//	カメラシェイクを処理するシステム
	//============================================================================
	class CameraShakeSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraShakeSystem() = default;
		~CameraShakeSystem() override = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CameraShakeSystem"; }
	};
} // Engine