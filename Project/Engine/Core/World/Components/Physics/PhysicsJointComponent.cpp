#include "PhysicsJointComponent.h"

namespace {

	Engine::UUID ReadLocalFileID(const nlohmann::json& in) {

		const std::string value = in.value("connectedBodyLocalFileID", "");
		return value.empty() ? Engine::UUID{} : Engine::FromString16Hex(value);
	}

	void WriteCommon(nlohmann::json& out, Engine::UUID connectedBody,
		const Engine::Vector3& anchor, const Engine::Vector3& connectedAnchor,
		bool autoConfigureConnectedAnchor, bool enabled) {

		out["connectedBodyLocalFileID"] = connectedBody ? Engine::ToString(connectedBody) : "";
		out["anchor"] = anchor.ToJson();
		out["connectedAnchor"] = connectedAnchor.ToJson();
		out["autoConfigureConnectedAnchor"] = autoConfigureConnectedAnchor;
		out["enabled"] = enabled;
	}
}

void Engine::from_json(const nlohmann::json& in, FixedJointComponent& component) {

	component.connectedBodyLocalFileID = ReadLocalFileID(in);
	if (in.contains("anchor")) { component.anchor = Vector3::FromJson(in["anchor"]); }
	if (in.contains("connectedAnchor")) {
		component.connectedAnchor = Vector3::FromJson(in["connectedAnchor"]);
	}
	component.autoConfigureConnectedAnchor = in.value(
		"autoConfigureConnectedAnchor", component.autoConfigureConnectedAnchor);
	component.enabled = in.value("enabled", component.enabled);
	component.runtimeInitialized = false;
}

void Engine::to_json(nlohmann::json& out, const FixedJointComponent& component) {

	WriteCommon(out, component.connectedBodyLocalFileID, component.anchor,
		component.connectedAnchor, component.autoConfigureConnectedAnchor, component.enabled);
}

void Engine::from_json(const nlohmann::json& in, HingeJointComponent& component) {

	component.connectedBodyLocalFileID = ReadLocalFileID(in);
	if (in.contains("anchor")) { component.anchor = Vector3::FromJson(in["anchor"]); }
	if (in.contains("connectedAnchor")) {
		component.connectedAnchor = Vector3::FromJson(in["connectedAnchor"]);
	}
	if (in.contains("axis")) { component.axis = Vector3::FromJson(in["axis"]); }
	component.minAngle = in.value("minAngle", component.minAngle);
	component.maxAngle = in.value("maxAngle", component.maxAngle);
	component.autoConfigureConnectedAnchor = in.value(
		"autoConfigureConnectedAnchor", component.autoConfigureConnectedAnchor);
	component.useLimits = in.value("useLimits", component.useLimits);
	component.enabled = in.value("enabled", component.enabled);
	component.runtimeInitialized = false;
}

void Engine::to_json(nlohmann::json& out, const HingeJointComponent& component) {

	WriteCommon(out, component.connectedBodyLocalFileID, component.anchor,
		component.connectedAnchor, component.autoConfigureConnectedAnchor, component.enabled);
	out["axis"] = component.axis.ToJson();
	out["minAngle"] = component.minAngle;
	out["maxAngle"] = component.maxAngle;
	out["useLimits"] = component.useLimits;
}
