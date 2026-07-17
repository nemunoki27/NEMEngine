#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	UIInputSystem class
	//	Canvas配下のポインターとナビゲーション入力を処理する
	//============================================================================
	class UIInputSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIInputSystem() = default;
		~UIInputSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UIInputSystem"; }
	};
} // Engine
