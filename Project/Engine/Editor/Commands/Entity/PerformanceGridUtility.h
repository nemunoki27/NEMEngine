#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <vector>

namespace Engine::PerformanceGridUtility {

	// 各軸の配置数の上限
	inline constexpr int32_t kMaxGridCount = 512;
	// モデルとライトの合計数の上限
	inline constexpr size_t kMaxEntityCount = 1000000;

	// 配置数の上限を生成前に検証する
	bool IsValidGridCount(int32_t gridCountXZ, int32_t gridCountY, bool placePointLights, int32_t pointLightCount);
	// 配置可能なセル数からライト数を求める
	size_t CalculatePointLightCount(int32_t gridCountXZ, int32_t gridCountY, bool enabled, int32_t requestedCount);
	// 隣接ライトの色相をずらす
	Color4 MakePointLightColor(size_t index);
	// Scene内で生成済みのグリッドを収集する
	std::vector<Entity> FindPerformanceGridRoots(ECSWorld& world, UUID sceneInstanceID);

	// モデルの配置順と座標を列挙する
	template <typename Visitor>
	void VisitModelGrid(int32_t countXZ, int32_t countY, float width, Visitor&& visitor) {

		// 中央を原点にしてXZと高さの順に配置
		const float start = -static_cast<float>(countXZ - 1) * width * 0.5f;
		size_t index = 0;
		for (int32_t y = 0; y < countY; ++y) {
			for (int32_t z = 0; z < countXZ; ++z) {
				for (int32_t x = 0; x < countXZ; ++x) {
					visitor(index++, Vector3(start + static_cast<float>(x) * width, static_cast<float>(y) * width,
										 start + static_cast<float>(z) * width));
				}
			}
		}
	}

	// 指定数のライトをセル中央へ均等に配置する
	template <typename Visitor>
	void VisitPointLightGrid(int32_t countXZ, int32_t countY, float width, size_t count, Visitor&& visitor) {

		if (count == 0 || countXZ <= 1 || countY <= 0) {
			return;
		}
		// 段階的な比率でグリッド全体からセルを選ぶ
		const float start = -static_cast<float>(countXZ - 1) * width * 0.5f;
		const size_t side = static_cast<size_t>(countXZ - 1);
		const size_t cells = side * side * static_cast<size_t>(countY);
		size_t cell = 0;
		size_t index = 0;
		for (int32_t y = 0; y < countY; ++y) {
			for (int32_t z = 0; z + 1 < countXZ; ++z) {
				for (int32_t x = 0; x + 1 < countXZ; ++x) {

					const size_t previous = cell * count / cells;
					const size_t next = (++cell) * count / cells;
					if (next == previous) {
						continue;
					}
					visitor(index++, Vector3(start + (static_cast<float>(x) + 0.5f) * width, static_cast<float>(y) * width,
										 start + (static_cast<float>(z) + 0.5f) * width));
				}
			}
		}
	}

}
