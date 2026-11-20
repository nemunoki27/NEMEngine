#include "HexTilingComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace {

	template <typename T>
	void SetExcluded(std::vector<T>& values, T key, bool excluded) {

		const auto it = std::lower_bound(values.begin(), values.end(), key);
		if (it != values.end() && *it == key) {
			if (!excluded) values.erase(it);
		} else if (excluded) {
			values.insert(it, key);
		}
	}

	template <typename T>
	void ReadExclusions(const nlohmann::json& in, const char* key, std::vector<T>& values) {

		values = in.value(key, std::vector<T>{});
		std::sort(values.begin(), values.end());
		values.erase(std::unique(values.begin(), values.end()), values.end());
	}
}

//============================================================================
//	HexTilingComponent classMethods
//============================================================================
bool Engine::IsHexTilingSubMeshEnabled(const HexTilingComponent& component, const SubMeshMaterial& subMesh, uint32_t index) {

	if (subMesh.stableID) {
		return !std::binary_search(component.excludedSubMeshIDs.begin(), component.excludedSubMeshIDs.end(), subMesh.stableID.value);
	}
	return !std::binary_search(component.excludedSubMeshIndices.begin(), component.excludedSubMeshIndices.end(), index);
}

void Engine::SetHexTilingSubMeshEnabled(HexTilingComponent& component, const SubMeshMaterial& subMesh, uint32_t index, bool enabled) {

	if (subMesh.stableID) {
		SetExcluded(component.excludedSubMeshIDs, subMesh.stableID.value, !enabled);
	} else {
		SetExcluded(component.excludedSubMeshIndices, index, !enabled);
	}
}

Engine::Vector4 Engine::ResolveHexTiling(const ECSWorld& world, Entity entity, int32_t subMeshIndex) {

	const auto* component = world.TryGetComponent<HexTilingComponent>(entity);
	if (!component || !component->enabled) return {0.0f, 0.0f, 0.0f, 0.0f};
	if (0 <= subMeshIndex && (!component->excludedSubMeshIDs.empty() || !component->excludedSubMeshIndices.empty())) {
		const auto subMeshes = GetMeshSubMeshes(world, entity);
		const auto index = static_cast<uint32_t>(subMeshIndex);
		if (index < subMeshes.size() && !IsHexTilingSubMeshEnabled(*component, subMeshes[index], index)) return {0.0f, 0.0f, 0.0f, 0.0f};
	}
	return { component->tileSize, component->randomRotation * (std::numbers::pi_v<float> / 180.0f),
		std::bit_cast<float>(component->seed), 0.0f };
}

void Engine::from_json(const nlohmann::json& in, HexTilingComponent& component) {

	component.enabled = in.value("enabled", true);
	const float size = in.value("tileSize", 1.0f);
	component.tileSize = std::isfinite(size) ? std::max(0.001f, size) : 1.0f;
	const float rotation = in.value("randomRotation", 0.0f);
	component.randomRotation = std::isfinite(rotation) ? std::clamp(rotation, 0.0f, 180.0f) : 0.0f;
	component.seed = in.value("seed", 0);
	ReadExclusions(in, "excludedSubMeshIDs", component.excludedSubMeshIDs);
	ReadExclusions(in, "excludedSubMeshIndices", component.excludedSubMeshIndices);
}

void Engine::to_json(nlohmann::json& out, const HexTilingComponent& component) {

	out = {{"enabled", component.enabled}, {"tileSize", component.tileSize}, {"randomRotation", component.randomRotation},
		{"seed", component.seed}, {"excludedSubMeshIDs", component.excludedSubMeshIDs},
		{"excludedSubMeshIndices", component.excludedSubMeshIndices}};
}
