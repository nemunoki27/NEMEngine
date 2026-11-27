#pragma once

#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

#include <vector>

namespace Engine {

	struct MaterialPaintPositionImage;
	struct MaterialPaintTriangle;

	struct MaterialPaintRegion {
		uint32_t left = UINT32_MAX, top = UINT32_MAX, right = 0, bottom = 0;
		bool Empty() const { return right <= left || bottom <= top; }
	};
	struct MaterialPaintBrush {
		float radius = 0.5f;
		float hardness = 0.5f;
		float strength = 1.0f;
		float angle = 0;
		std::vector<uint8_t> shape;
		uint32_t shapeWidth = 0, shapeHeight = 0;

		bool Stamp(const MaterialPaintTriangle& triangle, const Matrix4x4& world, const MaterialPaintPositionImage& surface,
			Vector3 center, Vector3 normal, bool erase, uint32_t channel, uint32_t resolution,
			std::vector<uint8_t>& mask, MaterialPaintRegion* dirty = nullptr, int* nextRow = nullptr) const;
	};
}
