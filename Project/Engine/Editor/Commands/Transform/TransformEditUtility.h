#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

//============================================================================
//	TransformEditUtility namespace
//============================================================================
namespace Engine::TransformEditUtility {

	// 
	bool ApplyImmediate(ECSWorld& world, const Entity& entity, const TransformComponent& transform);
} // Engine