#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	ReflectionProbeComponent struct
	//	保存する環境反射の範囲を指定する
	//============================================================================
	struct ReflectionProbeComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Lighting;
		uint32_t shape = 0;
		float radius = 10.0f;
		Vector3 extents = Vector3::AnyInit(10.0f);
		float intensity = 1.0f;
		bool enabled = true;
	};

	void from_json(const nlohmann::json& in, ReflectionProbeComponent& component);
	void to_json(nlohmann::json& out, const ReflectionProbeComponent& component);
}
