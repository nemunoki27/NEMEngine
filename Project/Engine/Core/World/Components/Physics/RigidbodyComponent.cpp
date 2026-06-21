#include "RigidbodyComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	RigidbodyComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, RigidbodyComponent& component) {

	component.bodyType = EnumAdapter<RigidbodyType>::FromString(
		in.value("bodyType", "Dynamic")).value_or(RigidbodyType::Dynamic);
	component.mass = in.value("mass", component.mass);
	component.useGravity = in.value("useGravity", component.useGravity);
	component.gravityScale = in.value("gravityScale", component.gravityScale);
	component.linearDamping = in.value("linearDamping", component.linearDamping);
	component.freezePositionX = in.value("freezePositionX", component.freezePositionX);
	component.freezePositionY = in.value("freezePositionY", component.freezePositionY);
	component.freezePositionZ = in.value("freezePositionZ", component.freezePositionZ);
	if (in.contains("linearVelocity")) {
		component.linearVelocity = Vector3::FromJson(in["linearVelocity"]);
	}
}

void Engine::to_json(nlohmann::json& out, const RigidbodyComponent& component) {

	// 蓄積力は実行時のみで保存しない
	out["bodyType"] = EnumAdapter<RigidbodyType>::ToString(component.bodyType);
	out["mass"] = component.mass;
	out["useGravity"] = component.useGravity;
	out["gravityScale"] = component.gravityScale;
	out["linearDamping"] = component.linearDamping;
	out["freezePositionX"] = component.freezePositionX;
	out["freezePositionY"] = component.freezePositionY;
	out["freezePositionZ"] = component.freezePositionZ;
	out["linearVelocity"] = component.linearVelocity.ToJson();
}
