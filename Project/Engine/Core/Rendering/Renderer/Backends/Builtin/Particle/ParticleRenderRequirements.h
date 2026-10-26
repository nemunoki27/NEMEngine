#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
// c++
#include <span>

namespace Engine {

	class RenderSceneBatch;
	struct RenderItem;
}

namespace Engine::ParticleRenderRequirements {

	// 可視粒子が背景深度のコピーを必要とするか調べる
	bool RequiresOpaqueDepth(const RenderSceneBatch& batch, std::span<const RenderItem* const> items,
		Entity camera = Entity::Null(), Entity mainCamera = Entity::Null());
	// 可視粒子が平行光源の影を必要とするか調べる
	bool RequiresDirectionalShadows(const RenderSceneBatch& batch, std::span<const RenderItem* const> items);
}
