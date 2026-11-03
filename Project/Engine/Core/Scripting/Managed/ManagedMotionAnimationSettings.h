#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <json.hpp>

namespace Engine {
	class ECSWorld;
	bool ReadMotionAnimationSettings(ECSWorld& world, Entity entity, nlohmann::json& data);
	bool ApplyMotionAnimationSettings(ECSWorld& world, Entity entity, const nlohmann::json& data);
}
