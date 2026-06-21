#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	PhysicsSystem class
	//	Rigidbodyの速度と力を積分してTransformを動かすシステム
	//============================================================================
	class PhysicsSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PhysicsSystem() = default;
		~PhysicsSystem() override = default;

		// 固定ステップで剛体を積分する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "PhysicsSystem"; }
	};
} // Engine
