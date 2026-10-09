#include "CameraComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>

// c++
#include <algorithm>

namespace {

	void ReadCommon(const nlohmann::json& in, Engine::CameraCommon& common) {

		common.priority = in.value("priority", common.priority);
		common.cullingMask = in.value("cullingMask", common.cullingMask);
		common.enabled = in.value("enabled", common.enabled);
		common.isMain = in.value("isMain", common.isMain);
		common.viewportX = in.value("viewportX", common.viewportX);
		common.viewportY = in.value("viewportY", common.viewportY);
		common.viewportWidth = in.value("viewportWidth", common.viewportWidth);
		common.viewportHeight = in.value("viewportHeight", common.viewportHeight);
		common.targetTexture = Engine::ParseAssetReference(
			in, "targetTexture", nullptr, Engine::AssetType::RenderTexture);
		common.postProcessEnabled = in.value("postProcessEnabled", common.postProcessEnabled);
		// Camera内の保存値から露出と色補正を復元する
		common.colorPipeline = Engine::RenderFeatureProfileSerializer::FromJson(in).colorPipeline;
		common.renderPasses = Engine::ParseAssetReference(
			in, "renderPasses", nullptr, Engine::AssetType::RenderPasses);
	}

	void WriteCommon(nlohmann::json& out, const Engine::CameraCommon& common) {

		out["priority"] = common.priority;
		out["cullingMask"] = common.cullingMask;
		out["enabled"] = common.enabled;
		out["isMain"] = common.isMain;
		out["viewportX"] = common.viewportX;
		out["viewportY"] = common.viewportY;
		out["viewportWidth"] = common.viewportWidth;
		out["viewportHeight"] = common.viewportHeight;
		out["targetTexture"] = Engine::ToAssetReferenceJson(common.targetTexture);
		out["postProcessEnabled"] = common.postProcessEnabled;
		// 設定値をCameraと同じ文書へ保存する
		Engine::RenderFeatureProfileAsset settings{};
		settings.colorPipeline = common.colorPipeline;
		out["colorPipeline"] = Engine::RenderFeatureProfileSerializer::ToJson(settings)["colorPipeline"];
		out["renderPasses"] = Engine::ToAssetReferenceJson(common.renderPasses);
	}
}

//============================================================================
//	CameraComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, OrthographicCameraComponent& component) {

	component.nearClip = in.value("nearClip", component.nearClip);
	component.farClip = in.value("farClip", component.farClip);
	ReadCommon(in, component.common);
}

void Engine::to_json(nlohmann::json& out, const OrthographicCameraComponent& component) {

	out["nearClip"] = component.nearClip;
	out["farClip"] = component.farClip;
	WriteCommon(out, component.common);
}

void Engine::from_json(const nlohmann::json& in, PerspectiveCameraComponent& component) {

	component.useGlobalIllumination = in.value("useGlobalIllumination", component.useGlobalIllumination);

	component.projectionMode = EnumAdapter<CameraProjectionMode>::FromString(in.value("projectionMode",
		std::string(EnumAdapter<CameraProjectionMode>::ToString(component.projectionMode)))).value_or(component.projectionMode);
	component.fovY = in.value("fovY", component.fovY);
	component.orthographicSize = in.value("orthographicSize", component.orthographicSize);
	component.nearClip = in.value("nearClip", component.nearClip);
	component.farClip = in.value("farClip", component.farClip);
	ReadCommon(in, component.common);
	component.common.editorFrustumScale = in.value("editorFrustumScale", component.common.editorFrustumScale);
}

void Engine::to_json(nlohmann::json& out, const PerspectiveCameraComponent& component) {

	out["useGlobalIllumination"] = component.useGlobalIllumination;

	out["projectionMode"] = EnumAdapter<CameraProjectionMode>::ToString(component.projectionMode);
	out["fovY"] = component.fovY;
	out["orthographicSize"] = component.orthographicSize;
	out["nearClip"] = component.nearClip;
	out["farClip"] = component.farClip;
	WriteCommon(out, component.common);
	out["editorFrustumScale"] = component.common.editorFrustumScale;
}
