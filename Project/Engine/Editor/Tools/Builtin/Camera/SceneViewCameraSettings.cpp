#include "SceneViewCameraSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Engine::SceneViewCameraDefaults;
using Engine::SceneViewCameraSettingsUtility::IsFinite;

const Engine::Vector3 Engine::SceneViewCameraDefaults::kDefault2DPosition = Engine::Vector3::AnyInit(0.0f);
const Engine::Vector3 Engine::SceneViewCameraDefaults::kDefaultPosition = Engine::Vector3(-6.8f, 2.52f, -8.19f);
const Engine::Vector3 Engine::SceneViewCameraDefaults::kDefaultRotation = Engine::Vector3(13.75f, 37.8f, 0.0f);

namespace {
	// JSONの数値を有限なfloatとして読み込む
	float ReadFiniteFloat(const nlohmann::json& data, const char* key, float defaultValue) {

		const auto it = data.find(key);
		if (it == data.end() || !it->is_number()) {
			return defaultValue;
		}

		const float value = static_cast<float>(it->get<double>());
		return std::isfinite(value) ? value : defaultValue;
	}

	// JSONの整数をint32_tの範囲内で読み込む
	int32_t ReadInt32(const nlohmann::json& data, const char* key, int32_t defaultValue) {

		const auto it = data.find(key);
		if (it == data.end() || !it->is_number_integer()) {
			return defaultValue;
		}

		const double value = it->get<double>();
		if (value < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
			value > static_cast<double>((std::numeric_limits<int32_t>::max)())) {
			return defaultValue;
		}
		return static_cast<int32_t>(value);
	}
}

bool Engine::SceneViewCameraSettingsUtility::IsFinite(const Vector3& value) {

	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Engine::SceneViewCameraSettings::SceneViewCameraSettings() {

	// 描画用の初期位置と投影を揃える
	cameraState = {};

	cameraState.transform2D.pos = kDefault2DPosition;
	cameraState.transform2D.rotation = Vector3::AnyInit(0.0f);

	cameraState.transform3D.pos = kDefaultPosition;
	cameraState.transform3D.rotation = kDefaultRotation;

	cameraState.enableOrthographic = true;
	cameraState.orthoNearClip = kDefault2DNearClip;
	cameraState.orthoFarClip = kDefault2DFarClip;
	cameraState.orthographicZoom = kDefault2DZoom;
	cameraState.orthographicCullingMask = -1;

	cameraState.enablePerspective = true;
	cameraState.perspectiveFovY = kDefaultFovY;
	cameraState.perspectiveNearClip = kDefaultNearClip;
	cameraState.perspectiveFarClip = kDefaultFarClip;
	cameraState.perspectiveCullingMask = -1;
}

bool Engine::SceneViewCameraSettingsUtility::Load(const std::string& filePath, SceneViewCameraSettings& settings) {

	// 設定ファイルがなければ現在値を保つ
	const nlohmann::json data = JsonAdapter::Load(filePath, false);
	if (!data.is_object()) {
		return false;
	}

	// 2D
	{
		settings.cameraState.transform2D.pos =
			JsonAdapter::GetVector3(data, "transform2D.pos", settings.cameraState.transform2D.pos);
		settings.cameraState.orthographicZoom = std::clamp(
			ReadFiniteFloat(data, "orthographicZoom", settings.cameraState.orthographicZoom), kMin2DZoom, kMax2DZoom);
		settings.cameraState.orthoNearClip = ReadFiniteFloat(data, "orthoNearClip", settings.cameraState.orthoNearClip);
		settings.cameraState.orthoFarClip = ReadFiniteFloat(data, "orthoFarClip", settings.cameraState.orthoFarClip);
		settings.cameraState.orthographicCullingMask =
			ReadInt32(data, "orthographicCullingMask", settings.cameraState.orthographicCullingMask);
		if (settings.cameraState.orthoFarClip <= settings.cameraState.orthoNearClip) {
			settings.cameraState.orthoFarClip = kDefault2DFarClip;
		}
	}
	// 3D
	{
		settings.cameraState.transform3D.pos =
			JsonAdapter::GetVector3(data, "transform3D.pos", settings.cameraState.transform3D.pos);
		settings.cameraState.transform3D.rotation =
			JsonAdapter::GetVector3(data, "transform3D.rotation", settings.cameraState.transform3D.rotation);
		settings.cameraState.perspectiveFovY =
			std::clamp(ReadFiniteFloat(data, "perspectiveFovY", settings.cameraState.perspectiveFovY), 1.0f, 179.0f);
		settings.cameraState.perspectiveNearClip =
			ReadFiniteFloat(data, "perspectiveNearClip", settings.cameraState.perspectiveNearClip);
		settings.cameraState.perspectiveFarClip =
			ReadFiniteFloat(data, "perspectiveFarClip", settings.cameraState.perspectiveFarClip);
		settings.cameraState.perspectiveCullingMask =
			ReadInt32(data, "perspectiveCullingMask", settings.cameraState.perspectiveCullingMask);
		if (settings.cameraState.perspectiveNearClip <= 0.0f) {
			settings.cameraState.perspectiveNearClip = kDefaultNearClip;
		}
		if (settings.cameraState.perspectiveFarClip <= settings.cameraState.perspectiveNearClip) {
			settings.cameraState.perspectiveFarClip = kDefaultFarClip;
		}
	}
	// 欠損した操作速度は現在値を保つ
	{
		settings.zoomRate2D = std::max(0.0f, ReadFiniteFloat(data, "zoomRate2D", settings.zoomRate2D));
		settings.panSpeed2D = std::max(0.0f, ReadFiniteFloat(data, "panSpeed2D", settings.panSpeed2D));
		settings.rotateSpeed = std::max(0.0f, ReadFiniteFloat(data, "rotateSpeed", settings.rotateSpeed));
		settings.zoomRate = std::max(0.0f, ReadFiniteFloat(data, "zoomRate", settings.zoomRate));
		settings.panSpeed = std::max(0.0f, ReadFiniteFloat(data, "panSpeed", settings.panSpeed));
	}
	return true;
}

bool Engine::SceneViewCameraSettingsUtility::Save(const std::string& filePath, const SceneViewCameraSettings& source) {

	SceneViewCameraSettings settings = source;
	// 非有限値と無効な投影値を補正する
	if (!IsFinite(settings.cameraState.transform2D.pos)) {
		settings.cameraState.transform2D.pos = kDefault2DPosition;
	}
	settings.cameraState.orthographicZoom = std::clamp(
		std::isfinite(settings.cameraState.orthographicZoom) ? settings.cameraState.orthographicZoom : kDefault2DZoom,
		kMin2DZoom, kMax2DZoom);
	if (!std::isfinite(settings.cameraState.orthoNearClip)) {
		settings.cameraState.orthoNearClip = kDefault2DNearClip;
	}
	if (!std::isfinite(settings.cameraState.orthoFarClip) ||
		settings.cameraState.orthoFarClip <= settings.cameraState.orthoNearClip) {
		settings.cameraState.orthoFarClip = kDefault2DFarClip;
	}
	if (!IsFinite(settings.cameraState.transform3D.pos)) {
		settings.cameraState.transform3D.pos = kDefaultPosition;
	}
	if (!IsFinite(settings.cameraState.transform3D.rotation)) {
		settings.cameraState.transform3D.rotation = kDefaultRotation;
	}
	settings.cameraState.perspectiveFovY =
		std::clamp(std::isfinite(settings.cameraState.perspectiveFovY) ? settings.cameraState.perspectiveFovY : kDefaultFovY,
			1.0f, 179.0f);
	if (!std::isfinite(settings.cameraState.perspectiveNearClip) || settings.cameraState.perspectiveNearClip <= 0.0f) {
		settings.cameraState.perspectiveNearClip = kDefaultNearClip;
	}
	if (!std::isfinite(settings.cameraState.perspectiveFarClip) ||
		settings.cameraState.perspectiveFarClip <= settings.cameraState.perspectiveNearClip) {
		settings.cameraState.perspectiveFarClip = kDefaultFarClip;
	}
	settings.rotateSpeed =
		std::isfinite(settings.rotateSpeed) && settings.rotateSpeed >= 0.0f ? settings.rotateSpeed : kDefaultRotateSpeed;
	settings.zoomRate = std::isfinite(settings.zoomRate) && settings.zoomRate >= 0.0f ? settings.zoomRate : kDefaultZoomRate;
	settings.panSpeed = std::isfinite(settings.panSpeed) && settings.panSpeed >= 0.0f ? settings.panSpeed : kDefaultPanSpeed;
	settings.zoomRate2D =
		std::isfinite(settings.zoomRate2D) && settings.zoomRate2D >= 0.0f ? settings.zoomRate2D : kDefault2DZoomRate;
	settings.panSpeed2D =
		std::isfinite(settings.panSpeed2D) && settings.panSpeed2D >= 0.0f ? settings.panSpeed2D : kDefault2DPanSpeed;

	// 位置と操作速度を保存用JSONへ変換する
	nlohmann::json data{};

	// 2D
	{
		JsonAdapter::SetVector3(data, "transform2D.pos", settings.cameraState.transform2D.pos);
		data["orthographicZoom"] = settings.cameraState.orthographicZoom;
		data["orthoNearClip"] = settings.cameraState.orthoNearClip;
		data["orthoFarClip"] = settings.cameraState.orthoFarClip;
		data["orthographicCullingMask"] = settings.cameraState.orthographicCullingMask;
		data["zoomRate2D"] = settings.zoomRate2D;
		data["panSpeed2D"] = settings.panSpeed2D;
	}
	// 3D
	{
		JsonAdapter::SetVector3(data, "transform3D.pos", settings.cameraState.transform3D.pos);
		JsonAdapter::SetVector3(data, "transform3D.rotation", settings.cameraState.transform3D.rotation);
		data["perspectiveFovY"] = settings.cameraState.perspectiveFovY;
		data["perspectiveNearClip"] = settings.cameraState.perspectiveNearClip;
		data["perspectiveFarClip"] = settings.cameraState.perspectiveFarClip;
		data["perspectiveCullingMask"] = settings.cameraState.perspectiveCullingMask;
	}
	// カメラ操作速度
	{
		data["rotateSpeed"] = settings.rotateSpeed;
		data["zoomRate"] = settings.zoomRate;
		data["panSpeed"] = settings.panSpeed;
	}

	return JsonAdapter::SaveCanonical(filePath, data);
}
