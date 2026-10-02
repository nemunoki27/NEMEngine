#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	JointConstraintSystem class
	//============================================================================
	class JointConstraintSystem :
		public ISystem {
	public:
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "JointConstraintSystem"; }
	};
} // Engine
