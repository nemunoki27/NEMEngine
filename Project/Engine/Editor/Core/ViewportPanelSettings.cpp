#include "ViewportPanelSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <filesystem>
#include <optional>

namespace {

	constexpr const char* kViewportPanelStateConfigPath = Engine::ConfigPaths::kViewportPanel;

	template <typename Enum>
	void LoadEnumValue(const nlohmann::json& data, const char* key, Enum& value) {

		if (!data.contains(key) || !data[key].is_string()) {
			return;
		}

		if (std::optional<Enum> loaded = Engine::EnumAdapter<Enum>::FromString(
			data[key].get<std::string>())) {
			value = loaded.value();
		}
	}
}

void Engine::ViewportPanelSettings::Load(EditorState& state) {

	const std::filesystem::path configPath = RuntimePaths::GetUserSettingsPath(kViewportPanelStateConfigPath);
	if (!JsonAdapter::Check(configPath.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath.string(), false);
	if (!data.is_object() || !data.contains("sceneView") || !data["sceneView"].is_object()) {
		return;
	}

	const nlohmann::json& sceneView = data["sceneView"];
	if (sceneView.contains("drawDefaultGrid") && sceneView["drawDefaultGrid"].is_boolean()) {
		state.drawSceneViewDefaultGrid = sceneView["drawDefaultGrid"].get<bool>();
	}
	if (sceneView.contains("enableSnapEditEntity") && sceneView["enableSnapEditEntity"].is_boolean()) {
		state.enableSnapEditEntity = sceneView["enableSnapEditEntity"].get<bool>();
	}
	// ギズモのスナップ設定を読み込む
	if (sceneView.contains("snap") && sceneView["snap"].is_object()) {

		const nlohmann::json& snap = sceneView["snap"];
		auto loadAxis = [&snap](const char* key, GridSnapAxis& axis) {
			if (!snap.contains(key) || !snap[key].is_object()) {
				return;
			}
			const nlohmann::json& a = snap[key];
			if (a.contains("size") && a["size"].is_number()) {
				axis.size = a["size"].get<float>();
			}
			if (a.contains("absolute") && a["absolute"].is_boolean()) {
				axis.absolute = a["absolute"].get<bool>();
			}
			};
		EntitySnapSettings& s = state.snapSettings;
		loadAxis("translate2D", s.translate2D);
		loadAxis("rotate2D", s.rotate2D);
		loadAxis("scale2D", s.scale2D);
		loadAxis("translate3D", s.translate3D);
		loadAxis("rotate3D", s.rotate3D);
		loadAxis("scale3D", s.scale3D);
		if (snap.contains("drawGrid") && snap["drawGrid"].is_boolean()) {
			s.drawSnapGrid = snap["drawGrid"].get<bool>();
		}
	}

	LoadEnumValue(sceneView, "manipulatorMode", state.sceneViewManipulatorMode);
	LoadEnumValue(sceneView, "cameraMode", state.sceneViewCamera.mode);
	LoadEnumValue(sceneView, "pickDimension", state.sceneViewPickDimension);

	// 実体参照は起動時に持ち越さずモードだけを復元しカメラ指定は現在のシーンで選び直す
	state.sceneViewCamera.ClearAssignedCameras();
	state.ClearSelection();
}

void Engine::ViewportPanelSettings::Save(const EditorState& state) {

	nlohmann::json sceneView = nlohmann::json::object();
	sceneView["drawDefaultGrid"] = state.drawSceneViewDefaultGrid;
	sceneView["enableSnapEditEntity"] = state.enableSnapEditEntity;
	// ギズモのスナップ設定を書き出す
	{
		auto saveAxis = [](const GridSnapAxis& axis) {
			return nlohmann::json{ { "size", axis.size }, { "absolute", axis.absolute } };
			};
		const EntitySnapSettings& s = state.snapSettings;
		nlohmann::json snap = nlohmann::json::object();
		snap["translate2D"] = saveAxis(s.translate2D);
		snap["rotate2D"] = saveAxis(s.rotate2D);
		snap["scale2D"] = saveAxis(s.scale2D);
		snap["translate3D"] = saveAxis(s.translate3D);
		snap["rotate3D"] = saveAxis(s.rotate3D);
		snap["scale3D"] = saveAxis(s.scale3D);
		snap["drawGrid"] = s.drawSnapGrid;
		sceneView["snap"] = snap;
	}
	sceneView["manipulatorMode"] = EnumAdapter<SceneViewManipulatorMode>::ToString(state.sceneViewManipulatorMode);
	sceneView["cameraMode"] = EnumAdapter<SceneViewCameraMode>::ToString(state.sceneViewCamera.mode);
	sceneView["pickDimension"] = EnumAdapter<SceneViewPickDimension>::ToString(
		state.sceneViewPickDimension);

	nlohmann::json data = nlohmann::json::object();
	data["sceneView"] = sceneView;

	const std::filesystem::path configPath = RuntimePaths::GetUserSettingsPath(kViewportPanelStateConfigPath);
	JsonAdapter::Save(configPath.string(), data);
}
