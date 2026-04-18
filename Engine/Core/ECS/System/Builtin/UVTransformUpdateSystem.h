#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

namespace Engine {

	//============================================================================
	//	UVTransformUpdateSystem class
	//	UVトランスフォームの更新、管理を行うシステム
	//============================================================================
	class UVTransformUpdateSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		UVTransformUpdateSystem() = default;
		~UVTransformUpdateSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UVTransformUpdateSystem"; }
	};
} // Engine