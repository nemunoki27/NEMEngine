#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Engine;

namespace {

	struct SplineEvaluationCache {

		struct Entry {

			uint64_t revision = 0;
			Matrix4x4 matrix = Matrix4x4::Identity();
			std::vector<SplineSample> samples;
		};
		std::unordered_map<uint64_t, Entry> entries;
	};

	// ABIのサイズを確認して値を転送する
	template <typename T>
	int32_t CopyValue(void* out, int32_t size, const T& value) {

		if (!out || size != sizeof(T)) return 0;
		std::memcpy(out, &value, sizeof(T));
		return 1;
	}
}

int32_t ManagedScriptRuntime::SplineGetPropertyCallback(ManagedNativeEntity entity, int32_t propertyID,
	int32_t index, void* out, int32_t size) {

	auto* world = ResolveWorld(entity);
	const auto* data = world ? TryGetSpline(*world, ResolveEntity(entity)) : nullptr;
	if (!data) return 0;
	if (propertyID == 0) return CopyValue(out, size, static_cast<int32_t>(data->points.size()));
	if (propertyID == 1) return CopyValue(out, size, data->closed ? 1 : 0);
	if (propertyID == 2) return CopyValue(out, size, data->tolerance);
	if (propertyID == 3 && index >= 0 && static_cast<size_t>(index) < data->points.size())
		return CopyValue(out, size, data->points[static_cast<size_t>(index)]);
	return 0;
}

int32_t ManagedScriptRuntime::SplineSetPropertyCallback(ManagedNativeEntity entity, int32_t propertyID,
	int32_t index, const void* value, int32_t size) {

	auto* world = ResolveWorld(entity);
	const auto target = ResolveEntity(entity);
	const auto* current = world ? TryGetSpline(*world, target) : nullptr;
	if (!current || !value) return 0;
	SplineData data = *current;
	if (propertyID == 0 && size == 4) {
		int32_t count;
		std::memcpy(&count, value, 4);
		if (count < 0 || count > 4096) return 0;
		data.points.resize(static_cast<size_t>(count));
	} else if (propertyID == 1 && size == 4) {
		int32_t closed;
		std::memcpy(&closed, value, 4);
		data.closed = closed != 0;
	} else if (propertyID == 2 && size == 4) std::memcpy(&data.tolerance, value, 4);
	else if (propertyID == 3 && size == sizeof(SplineControlPoint) && index >= 0 && static_cast<size_t>(index) < data.points.size())
		std::memcpy(&data.points[static_cast<size_t>(index)], value, sizeof(SplineControlPoint));
	else return 0;
	try { SetSpline(*world, target, data); }
	catch (const std::exception&) { return 0; }
	return 1;
}

int32_t ManagedScriptRuntime::SplineEvaluateCallback(ManagedNativeEntity entity, float distance,
	int32_t loop, void* out, int32_t size) {

	if (!std::isfinite(distance)) return 0;
	auto* world = ResolveWorld(entity);
	if (!world) return 0;
	const auto target = ResolveEntity(entity);
	const auto* component = world->TryGetComponent<SplineComponent>(target);
	const auto* transform = world->TryGetComponent<TransformComponent>(target);
	const auto* storage = world->GetStorage().TryGet<SplineStorage>();
	const auto* record = component && storage ? storage->TryGet(component->handle) : nullptr;
	if (!record || !transform) return 0;
	auto& entries = world->GetStorage().Get<SplineEvaluationCache>().entries;
	const auto key = static_cast<uint64_t>(component->handle.generation) << 32 | component->handle.index;
	// 新しい参照を作る前に削除済み曲線のサンプルを解放する
	if (!entries.contains(key)) std::erase_if(entries, [&](const auto& entry) {
		return !storage->IsAlive(SplineStorage::Handle{static_cast<uint32_t>(entry.first), static_cast<uint32_t>(entry.first >> 32)});
	});
	auto& cache = entries[key];
	if (cache.revision != record->revision || cache.matrix != transform->worldMatrix) {
		cache.samples = BuildSplineSamples(record->value, transform->worldMatrix);
		cache.matrix = transform->worldMatrix;
		cache.revision = record->revision;
	}
	if (cache.samples.empty()) return 0;
	return CopyValue(out, size, EvaluateSplineDistance(cache.samples, distance, loop != 0));
}
