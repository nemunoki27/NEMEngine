#include "BakedLightingBakeState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

//============================================================================
//	BakedLighting volumeMethods
//============================================================================
void Engine::PrepareBakedLightingVolumes(BakedLightingBakeState& state) {

	auto& data = state.input.preparation.data;
	if (data.nodes.empty()) throw std::runtime_error("固定背景の探索構造が空です");
	const Vector3 minimum = data.nodes.front().minimum;
	const Vector3 maximum = data.nodes.front().maximum;
	float spacing = std::max(state.settings.probeSpacing, 0.1f);
	std::array<uint32_t, 3> dimensions{};
	// 大きいSceneでもProbe数を上限内に収める
	for (;;) {
		const Vector3 extent = maximum - minimum;
		dimensions = {uint32_t(std::ceil(extent.x / spacing)) + 3u, uint32_t(std::ceil(extent.y / spacing)) + 3u,
			uint32_t(std::ceil(extent.z / spacing)) + 3u};
		if (uint64_t(dimensions[0]) * dimensions[1] * dimensions[2] <= 4096u) break;
		spacing *= 1.25f;
	}
	data.probeSpacing = spacing;
	data.probeMinimum = minimum - Vector3::AnyInit(spacing);
	data.probeDimensions = dimensions;
	data.surfaceTolerance = std::max(state.settings.normalBias * 4.0f, 0.01f);
	for (uint32_t z = 0; z < dimensions[2]; ++z) {
		for (uint32_t y = 0; y < dimensions[1]; ++y) {
			for (uint32_t x = 0; x < dimensions[0]; ++x) {
				BakedLightingProbe probe{};
				probe.position = data.probeMinimum + Vector3(float(x), float(y), float(z)) * spacing;
				probe.radius = spacing;
				data.probes.push_back(probe);
			}
		}
	}
	uint32_t offset = 0;
	if (data.captures.size() > 64) throw std::runtime_error("Reflection Captureの数が上限を超えています");
	for (auto& capture : data.captures) {
		if (capture.shape > 1u || capture.radius <= 0.0f || std::min({capture.extents.x, capture.extents.y, capture.extents.z}) <= 0.0f) {
			throw std::runtime_error("Reflection Captureの形状または範囲が不正です");
		}
		capture.size = state.settings.captureSize;
		capture.dataOffset = offset;
		capture.mipCount = 1;
		offset += 6u * capture.size * capture.size;
	}
	data.reflectionPixels.resize(offset);
}
