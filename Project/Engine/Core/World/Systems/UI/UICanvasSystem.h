#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	UICanvasSystem class
	//	UI表示値の更新とTransform確定後のスクリーン行列構築を行う
	//============================================================================
	class UICanvasSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UICanvasSystem() = default;
		~UICanvasSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UICanvasSystem"; }
	};
} // Engine
