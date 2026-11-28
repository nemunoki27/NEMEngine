#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Engine {

	struct SceneExecutionContext;
	struct RaytracingGeometryBounds;
	struct Matrix4x4;

	class GIProbeSurfaceMask {
	public:
		using Brick = std::array<int32_t, 4>;
		struct Bounds {
			Vector4 sphere{0, 0, 0, -1};
			Vector3 halfExtent{-1, -1, -1};
			bool operator==(const Bounds&) const = default;

			uint64_t GetProbeMask(const Vector3& first, float spacing, float reach, uint64_t skip) const;
		};
		static bool TryGetWorldBounds(const RaytracingGeometryBounds& geometry, const Matrix4x4& world, Bounds& bounds);
		void Release();
		void Update(const SceneExecutionContext& context, std::span<const Brick> bricks, float baseSpacing,
			uint32_t coarseShift, std::vector<uint64_t>& masks);

	private:
		struct Cache;
		std::shared_ptr<Cache> cache_;
	};
}
