#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

namespace Engine {

	//============================================================================
	//	FixedJointComponent structure
	//============================================================================
	struct FixedJointComponent {

		UUID connectedBodyLocalFileID{};
		Vector3 anchor = Vector3::AnyInit(0.0f);
		Vector3 connectedAnchor = Vector3::AnyInit(0.0f);
		bool autoConfigureConnectedAnchor = true;
		bool enabled = true;
		bool runtimeInitialized = false;
		Quaternion runtimeRelativeRotation = Quaternion::Identity();
	};

	//============================================================================
	//	HingeJointComponent structure
	//============================================================================
	struct HingeJointComponent {

		UUID connectedBodyLocalFileID{};
		Vector3 anchor = Vector3::AnyInit(0.0f);
		Vector3 connectedAnchor = Vector3::AnyInit(0.0f);
		Vector3 axis = Vector3(0.0f, 1.0f, 0.0f);
		float minAngle = -180.0f;
		float maxAngle = 180.0f;
		bool autoConfigureConnectedAnchor = true;
		bool useLimits = false;
		bool enabled = true;
		bool runtimeInitialized = false;
		Quaternion runtimeRelativeRotation = Quaternion::Identity();
	};

	void from_json(const nlohmann::json& in, FixedJointComponent& component);
	void to_json(nlohmann::json& out, const FixedJointComponent& component);
	void from_json(const nlohmann::json& in, HingeJointComponent& component);
	void to_json(nlohmann::json& out, const HingeJointComponent& component);
} // Engine
