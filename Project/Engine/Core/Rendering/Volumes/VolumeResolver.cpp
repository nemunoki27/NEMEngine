#include "VolumeResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/VolumeComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

	struct VolumeCandidate {

		const Engine::VolumeProfileAsset* profile = nullptr;
		float priority = 0.0f;
		float weight = 0.0f;
	};

	float ResolveSpatialWeight(const Engine::VolumeComponent& volume,
		const Engine::TransformComponent& transform, const Engine::Vector3& cameraPos) {

		if (volume.global) {
			return 1.0f;
		}

		// Camera位置をVolumeのローカル座標へ変換
		const Engine::Vector3 localPos = Engine::Vector3::Transform(
			cameraPos, Engine::Matrix4x4::Inverse(transform.worldMatrix));
		const Engine::Vector3 halfSize{
			(std::max)(volume.size.x * 0.5f, 0.0f),
			(std::max)(volume.size.y * 0.5f, 0.0f),
			(std::max)(volume.size.z * 0.5f, 0.0f),
		};
		const Engine::Vector3 outside{
			(std::max)(std::abs(localPos.x) - halfSize.x, 0.0f),
			(std::max)(std::abs(localPos.y) - halfSize.y, 0.0f),
			(std::max)(std::abs(localPos.z) - halfSize.z, 0.0f),
		};
		const float distance = outside.Length();
		if (distance <= 0.0f) {
			return 1.0f;
		}
		if (volume.blendDistance <= 0.0f || volume.blendDistance <= distance) {
			return 0.0f;
		}
		return 1.0f - distance / volume.blendDistance;
	}
}

//============================================================================
//	VolumeResolver classMethods
//============================================================================

Engine::ColorPipelineSettings Engine::VolumeResolver::Resolve(
	ECSWorld& world, RenderAssetLibrary& assetLibrary, const ResolvedCameraView& camera) {

	ColorPipelineSettings result{};
	if (!camera.postProcessEnabled) {
		return result;
	}
	if (camera.volumeProfile) {
		if (const VolumeProfileAsset* profile = assetLibrary.LoadVolumeProfile(camera.volumeProfile)) {
			result = profile->colorPipeline;
		}
	}
	std::vector<VolumeCandidate> candidates{};
	world.ForEach<VolumeComponent, TransformComponent>(
		[&](Entity entity, VolumeComponent& volume, TransformComponent& transform) {

			const SceneObjectComponent* sceneObject = world.TryGetComponent<SceneObjectComponent>(entity);
			if (!volume.enabled || !volume.profile || (sceneObject && !sceneObject->activeInHierarchy) ||
				(volume.layerMask & camera.volumeLayerMask) == 0) {
				return;
			}

			const float weight = std::clamp(volume.weight, 0.0f, 1.0f) *
				ResolveSpatialWeight(volume, transform, camera.cameraPos);
			if (weight <= 0.0f) {
				return;
			}
			const VolumeProfileAsset* profile = assetLibrary.LoadVolumeProfile(volume.profile);
			if (profile) {
				candidates.push_back({ profile, volume.priority, weight });
			}
		});

	// 優先度の高いVolumeを後から合成
	std::stable_sort(candidates.begin(), candidates.end(), [](const VolumeCandidate& lhs, const VolumeCandidate& rhs) {
		return lhs.priority < rhs.priority;
		});
	for (const VolumeCandidate& candidate : candidates) {
		Blend(result, candidate.profile->colorPipeline, candidate.weight);
	}
	return result;
}

void Engine::VolumeResolver::Blend(
	ColorPipelineSettings& base, const ColorPipelineSettings& overlay, float weight) {

	weight = std::clamp(weight, 0.0f, 1.0f);
	if (weight <= 0.0f) {
		return;
	}

	// 離散値は重みが半分以上なら上書き
	if (weight >= 0.5f) {
		base.exposure.mode = overlay.exposure.mode;
		base.exposure.usePreExposure = overlay.exposure.usePreExposure;
	}
	base.exposure.manualEV100 = std::lerp(base.exposure.manualEV100, overlay.exposure.manualEV100, weight);
	base.exposure.compensation = std::lerp(base.exposure.compensation, overlay.exposure.compensation, weight);
	base.exposure.minEV100 = std::lerp(base.exposure.minEV100, overlay.exposure.minEV100, weight);
	base.exposure.maxEV100 = std::lerp(base.exposure.maxEV100, overlay.exposure.maxEV100, weight);
	base.exposure.histogramLowPercent = std::lerp(base.exposure.histogramLowPercent,
		overlay.exposure.histogramLowPercent, weight);
	base.exposure.histogramHighPercent = std::lerp(base.exposure.histogramHighPercent,
		overlay.exposure.histogramHighPercent, weight);
	base.exposure.speedUp = std::lerp(base.exposure.speedUp, overlay.exposure.speedUp, weight);
	base.exposure.speedDown = std::lerp(base.exposure.speedDown, overlay.exposure.speedDown, weight);

	base.filmic.slope = std::lerp(base.filmic.slope, overlay.filmic.slope, weight);
	base.filmic.toe = std::lerp(base.filmic.toe, overlay.filmic.toe, weight);
	base.filmic.shoulder = std::lerp(base.filmic.shoulder, overlay.filmic.shoulder, weight);
	base.filmic.blackClip = std::lerp(base.filmic.blackClip, overlay.filmic.blackClip, weight);
	base.filmic.whiteClip = std::lerp(base.filmic.whiteClip, overlay.filmic.whiteClip, weight);

	base.colorGrading.colorFilter = Color4::Lerp(
		base.colorGrading.colorFilter, overlay.colorGrading.colorFilter, weight);
	base.colorGrading.temperature = std::lerp(
		base.colorGrading.temperature, overlay.colorGrading.temperature, weight);
	base.colorGrading.tint = std::lerp(base.colorGrading.tint, overlay.colorGrading.tint, weight);
	base.colorGrading.saturation = Vector3::Lerp(
		base.colorGrading.saturation, overlay.colorGrading.saturation, weight);
	base.colorGrading.contrast = Vector3::Lerp(
		base.colorGrading.contrast, overlay.colorGrading.contrast, weight);
	base.colorGrading.gamma = Vector3::Lerp(base.colorGrading.gamma, overlay.colorGrading.gamma, weight);
	base.colorGrading.gain = Vector3::Lerp(base.colorGrading.gain, overlay.colorGrading.gain, weight);
	base.colorGrading.offset = Vector3::Lerp(base.colorGrading.offset, overlay.colorGrading.offset, weight);
}
