#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>

//============================================================================
//	TransformEditUtility namespace
//============================================================================
namespace Engine::TransformEditUtility {

	// 
	bool ApplyImmediate(ECSWorld& world, const Entity& entity, const TransformComponent& transform);
} // Engine