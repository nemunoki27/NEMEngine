#include "CameraComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	CameraComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, OrthographicCameraComponent& component) {

	component.nearClip = in.value("nearClip", component.nearClip);
	component.farClip = in.value("farClip", component.farClip);
	component.common.priority = in.value("priority", component.common.priority);
	component.common.cullingMask = in.value("cullingMask", component.common.cullingMask);
	component.common.enabled = in.value("enabled", component.common.enabled);
	component.common.isMain = in.value("isMain", component.common.isMain);
}

void Engine::to_json(nlohmann::json& out, const OrthographicCameraComponent& component) {

	out["nearClip"] = component.nearClip;
	out["farClip"] = component.farClip;
	out["priority"] = component.common.priority;
	out["cullingMask"] = component.common.cullingMask;
	out["enabled"] = component.common.enabled;
	out["isMain"] = component.common.isMain;
}

void Engine::from_json(const nlohmann::json& in, PerspectiveCameraComponent& component) {

	component.fovY = in.value("fovY", component.fovY);
	component.nearClip = in.value("nearClip", component.nearClip);
	component.farClip = in.value("farClip", component.farClip);
	component.common.priority = in.value("priority", component.common.priority);
	component.common.cullingMask = in.value("cullingMask", component.common.cullingMask);
	component.common.enabled = in.value("enabled", component.common.enabled);
	component.common.isMain = in.value("isMain", component.common.isMain);
	component.common.editorFrustumScale = in.value("editorFrustumScale", component.common.editorFrustumScale);
}

void Engine::to_json(nlohmann::json& out, const PerspectiveCameraComponent& component) {

	out["fovY"] = component.fovY;
	out["nearClip"] = component.nearClip;
	out["farClip"] = component.farClip;
	out["priority"] = component.common.priority;
	out["cullingMask"] = component.common.cullingMask;
	out["enabled"] = component.common.enabled;
	out["isMain"] = component.common.isMain;
	out["editorFrustumScale"] = component.common.editorFrustumScale;
}