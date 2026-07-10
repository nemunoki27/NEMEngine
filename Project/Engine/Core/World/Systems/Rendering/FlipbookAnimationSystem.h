#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	FlipbookAnimationSystem class
	//	連番画像アニメーションを進めてUVTransformへ反映するシステム
	//============================================================================
	class FlipbookAnimationSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FlipbookAnimationSystem() = default;
		~FlipbookAnimationSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "FlipbookAnimationSystem"; }
	};
} // Engine
