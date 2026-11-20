#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	// ワールド座標が確定した後でビームの経路を更新する
	class BeamRendererSystem : public ISystem {
	public:
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "BeamRendererSystem"; }
	};
}
