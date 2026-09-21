#include "FrameLightBatch.h"

//============================================================================
//	FrameLightBatch classMethods
//============================================================================
namespace {

	template <typename T>
	bool LightItemLess(const T& a, const T& b) {
		if (a.common.sceneInstanceID.value != b.common.sceneInstanceID.value) {
			return a.common.sceneInstanceID.value < b.common.sceneInstanceID.value;
		}
		if (a.common.entity.index != b.common.entity.index) {
			return a.common.entity.index < b.common.entity.index;
		}
		return a.common.entity.generation < b.common.entity.generation;
	}
}

void Engine::PerViewLightSet::Clear() {

	view = nullptr;
	camera = nullptr;
	sceneInstanceID = {};
	sourceRevision = 0;
	directionalLights.clear();
	pointLights.clear();
	rectLights.clear();
	spotLights.clear();
	hasShadowCastingLight = false;
}

bool Engine::PerViewLightSet::IsEmpty() const {

	return directionalLights.empty() && pointLights.empty() &&
		rectLights.empty() && spotLights.empty();
}

uint32_t Engine::PerViewLightSet::GetDirectionalCount() const {

	return static_cast<uint32_t>(directionalLights.size());
}

uint32_t Engine::PerViewLightSet::GetPointCount() const {

	return static_cast<uint32_t>(pointLights.size());
}

uint32_t Engine::PerViewLightSet::GetRectCount() const {

	return static_cast<uint32_t>(rectLights.size());
}

uint32_t Engine::PerViewLightSet::GetSpotCount() const {

	return static_cast<uint32_t>(spotLights.size());
}

uint32_t Engine::PerViewLightSet::GetLocalLightCount() const {

	return GetPointCount() + GetSpotCount() + GetRectCount();
}

uint32_t Engine::PerViewLightSet::GetTotalCount() const {

	return GetDirectionalCount() + GetPointCount() +
		GetSpotCount() + GetRectCount();
}

void Engine::FrameLightBatch::Add(DirectionalLightItem&& item) {

	directionalLights_.emplace_back(std::move(item));
}

void Engine::FrameLightBatch::Add(PointLightItem&& item) {

	pointLights_.emplace_back(std::move(item));
}

void Engine::FrameLightBatch::Add(RectLightItem&& item) {

	rectLights_.emplace_back(std::move(item));
}

void Engine::FrameLightBatch::Add(SpotLightItem&& item) {

	spotLights_.emplace_back(std::move(item));
}

void Engine::FrameLightBatch::Clear() {

	directionalLights_.clear();
	pointLights_.clear();
	rectLights_.clear();
	spotLights_.clear();
	sourceWorld_ = nullptr;
	sourceRevision_ = 0;
}

void Engine::FrameLightBatch::Sort() {

	std::sort(directionalLights_.begin(), directionalLights_.end(), LightItemLess<DirectionalLightItem>);
	std::sort(pointLights_.begin(), pointLights_.end(), LightItemLess<PointLightItem>);
	std::sort(rectLights_.begin(), rectLights_.end(), LightItemLess<RectLightItem>);
	std::sort(spotLights_.begin(), spotLights_.end(), LightItemLess<SpotLightItem>);
}

void Engine::FrameLightBatch::SetSource(const ECSWorld* world, uint64_t revision) {

	sourceWorld_ = world;
	sourceRevision_ = revision;
}

//============================================================================
//	FrameLightBatch classMethods
//============================================================================

namespace Engine {

	bool FrameLightBatch::MatchesSource(const ECSWorld* world, uint64_t revision) const {

		return sourceWorld_ == world && sourceRevision_ == revision;
	}
}
