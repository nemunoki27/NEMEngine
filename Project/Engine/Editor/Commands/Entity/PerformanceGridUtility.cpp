#include "PerformanceGridUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

// c++
#include <algorithm>
#include <cmath>
// imgui
#include <imgui.h>

Engine::Color4 Engine::PerformanceGridUtility::MakePointLightColor(size_t index) {

	// 隣接ライトの色相をずらす
	constexpr float goldenRatio = 0.61803398875f;
	const float hue = std::fmod(static_cast<float>(index) * goldenRatio, 1.0f);
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
	return Color4(r, g, b, 1.0f);
}

bool Engine::PerformanceGridUtility::IsValidGridCount(
	int32_t gridCountXZ, int32_t gridCountY, bool placePointLights, int32_t pointLightCount) {

	// 軸数を制限して乗算のオーバーフローを防ぐ
	if (gridCountXZ <= 0 || gridCountXZ > kMaxGridCount || gridCountY <= 0 || gridCountY > kMaxGridCount ||
		pointLightCount < 0 || static_cast<size_t>(pointLightCount) > kMaxEntityCount) {
		return false;
	}
	const size_t modelCount = static_cast<size_t>(gridCountXZ) * gridCountXZ * gridCountY;
	const size_t lightCount = CalculatePointLightCount(gridCountXZ, gridCountY, placePointLights, pointLightCount);
	return modelCount + lightCount <= kMaxEntityCount;
}

size_t Engine::PerformanceGridUtility::CalculatePointLightCount(
	int32_t gridCountXZ, int32_t gridCountY, bool enabled, int32_t requestedCount) {

	// 無効な配置条件ではライトを生成しない
	if (!enabled || gridCountXZ <= 1 || gridCountXZ > kMaxGridCount || gridCountY <= 0 || gridCountY > kMaxGridCount ||
		requestedCount <= 0) {
		return 0;
	}
	const size_t cellCount = static_cast<size_t>(gridCountXZ - 1);
	return (std::min)(cellCount * cellCount * static_cast<size_t>(gridCountY), static_cast<size_t>(requestedCount));
}

std::vector<Engine::Entity> Engine::PerformanceGridUtility::FindPerformanceGridRoots(
	Engine::ECSWorld& world, Engine::UUID sceneInstanceID) {

	// 同じSceneに属する生成ルートだけを収集
	std::vector<Engine::Entity> roots;
	world.ForEach<Engine::NameComponent, Engine::SceneObjectComponent, Engine::HierarchyComponent>(
		[&](const Engine::Entity& entity, Engine::NameComponent& name, Engine::SceneObjectComponent& sceneObject,
			Engine::HierarchyComponent& hierarchy) {
			if (name.name != "PerformanceGrid" || world.IsAlive(hierarchy.parent)) {
				return;
			}
			if (sceneInstanceID && sceneObject.sceneInstanceID != sceneInstanceID) {
				return;
			}
			roots.emplace_back(entity);
		});
	return roots;
}
