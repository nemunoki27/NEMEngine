#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	UVTransformSystem class
	//	UVトランスフォームの更新、管理を行うシステム
	//============================================================================
	class UVTransformSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UVTransformSystem() = default;
		~UVTransformSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UVTransformSystem"; }
	};
} // Engine