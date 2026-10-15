#include "BakedLightingGeometry.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	BakedLightingGeometry filterMethods
//============================================================================
void Engine::BakedLightingGeometry::FilterLightmaps(BakedLightingData& data,
	const std::vector<BakedLightingTexel>& texels, const std::atomic<bool>& cancelled) {

	const uint32_t size = data.atlasSize;
	std::vector<Vector4> filtered = data.lightmaps;
	for (uint32_t atlas = 0; atlas < data.atlasCount; ++atlas) {
		if (cancelled.load()) return;
		for (uint32_t y = 0; y < size; ++y) {
			for (uint32_t x = 0; x < size; ++x) {
				const size_t index = (size_t(atlas) * size + y) * size + x;
				if (!texels[index].valid) continue;
				const auto& center = texels[index];
				Vector4 color{};
				float totalWeight = 0.0f;
				// 面境界を越えず固定ノイズだけを平滑化する
				for (int dy = -2; dy <= 2; ++dy) {
					for (int dx = -2; dx <= 2; ++dx) {
						const int sx = int(x) + dx;
						const int sy = int(y) + dy;
						if (sx < 0 || sy < 0 || sx >= int(size) || sy >= int(size)) continue;
						const size_t sampleIndex = (size_t(atlas) * size + uint32_t(sy)) * size + uint32_t(sx);
						const auto& sample = texels[sampleIndex];
						if (!sample.valid || sample.chart != center.chart) continue;
						const float alignment = std::max(Vector3::Dot(center.normal, sample.normal), 0.0f);
						const float normalDistance = std::abs(Vector3::Dot(sample.position - center.position, center.normal));
						const float weight = std::exp(-float(dx * dx + dy * dy) * 0.25f - normalDistance * 32.0f) *
							std::pow(alignment, 16.0f);
						color += data.lightmaps[sampleIndex] * weight;
						totalWeight += weight;
					}
				}
				filtered[index] = color / std::max(totalWeight, 1e-6f);
				filtered[index].w = 1.0f;
			}
		}
	}
	data.lightmaps.swap(filtered);
	// Bilinearの参照先となるChartの余白を埋める
	std::vector<uint32_t> charts(texels.size(), UINT32_MAX);
	for (size_t index = 0; index < texels.size(); ++index) {
		if (texels[index].valid) charts[index] = texels[index].chart;
	}
	for (uint32_t step = 0; step < 6; ++step) {
		if (cancelled.load()) return;
		filtered = data.lightmaps;
		std::vector<uint32_t> nextCharts = charts;
		for (uint32_t atlas = 0; atlas < data.atlasCount; ++atlas) {
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					const size_t index = (size_t(atlas) * size + y) * size + x;
					if (charts[index] != UINT32_MAX) continue;
					for (const Vector2I offset : {Vector2I(-1, 0), Vector2I(1, 0), Vector2I(0, -1), Vector2I(0, 1)}) {
						const int sx = int(x) + offset.x;
						const int sy = int(y) + offset.y;
						if (sx < 0 || sy < 0 || sx >= int(size) || sy >= int(size)) continue;
						const size_t sampleIndex = (size_t(atlas) * size + uint32_t(sy)) * size + uint32_t(sx);
						if (charts[sampleIndex] == UINT32_MAX) continue;
						filtered[index] = data.lightmaps[sampleIndex];
						nextCharts[index] = charts[sampleIndex];
						break;
					}
				}
			}
		}
		data.lightmaps.swap(filtered);
		charts.swap(nextCharts);
	}
}
