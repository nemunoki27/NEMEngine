#include "MaterialPaintBrush.h"

//============================================================================
//	include
//============================================================================
#include "MaterialPaintSurface.h"
#include <Engine/Core/Rendering/Materials/MaterialPaintAtlas.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine {

	bool MaterialPaintBrush::Stamp(const MaterialPaintTriangle& triangle, const Matrix4x4& world,
		const MaterialPaintPositionImage& surface, Vector3 center, Vector3 normal, bool erase,
		uint32_t channel, uint32_t resolution, std::vector<uint8_t>& mask, MaterialPaintRegion* dirty, int* nextRow) const {

		const int resumeRow = nextRow ? *nextRow : -1;
		if (nextRow) *nextRow = -1;
		const Vector3 p = Vector3::Transform(triangle.positions[0], world);
		const Vector3 e = Vector3::Transform(triangle.positions[1], world) - p;
		const Vector3 f = Vector3::Transform(triangle.positions[2], world) - p;
		const Vector3 cross = Vector3::Cross(e, f);
		const float area = cross.Length();
		if (area < 1.0e-10f) return false;
		const Vector3 face = cross / area;
		const Vector3 delta = center - p;
		if (std::abs(Vector3::Dot(delta, face)) > radius || std::abs(Vector3::Dot(face, normal)) < 0.04f) return false;

		const Vector2 uv = triangle.uv[0], a = triangle.uv[1] - uv, b = triangle.uv[2] - uv;
		const float det = a.x * b.y - a.y * b.x;
		if (std::abs(det) < 1.0e-12f) return false;
		// ブラシ球に接するUV範囲で走査を絞る
		const Vector3 ds = Vector3::Cross(f, face) / area, dt = Vector3::Cross(face, e) / area;
		const float s = Vector3::Dot(ds, delta), t = Vector3::Dot(dt, delta);
		const Vector2 middle = uv + a * s + b * t;
		const Vector2 extent((ds * a.x + dt * b.x).Length() * radius, (ds * a.y + dt * b.y).Length() * radius);
		const auto& verts = triangle.uv;
		const int x0 = std::max(0, int(std::floor(resolution * std::max(middle.x - extent.x, std::min({uv.x,verts[1].x,verts[2].x})))));
		const int x1 = std::min(int(resolution) - 1, int(std::ceil(resolution * std::min(middle.x + extent.x, std::max({uv.x,verts[1].x,verts[2].x})))));
		const int y0 = std::max(0, int(std::floor(resolution * std::max(middle.y - extent.y, std::min({uv.y,verts[1].y,verts[2].y})))));
		const int y1 = std::min(int(resolution) - 1, int(std::ceil(resolution * std::min(middle.y + extent.y, std::max({uv.y,verts[1].y,verts[2].y})))));
		if (x1 < x0 || y1 < y0) return false;
		const Vector3 tangent = Vector3::NormalizeOr(Vector3::Cross(std::abs(normal.y) < 0.9f ?
			Vector3(0,1,0) : Vector3(0,0,1), normal), Vector3(1,0,0));
		const Vector3 bitangent = Vector3::Cross(normal, tangent);
		const float radians = angle * std::numbers::pi_v<float> / 180.0f;
		const float cs = std::cos(radians), sn = std::sin(radians);
		const float invSize = 1.0f / resolution, invDet = 1.0f / det;
		const float invRadius = 1.0f / radius, invFeather = 1.0f / std::max(0.0001f, 1.0f - hardness);
		const float stepS = b.y * invDet * invSize, stepT = -a.y * invDet * invSize;
		const Vector3 stepPoint = e * stepS + f * stepT;
		const float startU = (x0 + 0.5f) * invSize - uv.x;
		bool changed = false;
		const int firstRow = std::max(y0, resumeRow);
		const int lastRow = nextRow ? std::min(y1, firstRow + std::max(1, 4096 / (x1 - x0 + 1)) - 1) : y1;
		if (nextRow && lastRow < y1) *nextRow = lastRow + 1;
		for (int y = firstRow; y <= lastRow; ++y) {
			const float v = (y + 0.5f) * invSize - uv.y;
			const float s0 = (startU * b.y - v * b.x) * invDet, t0 = (a.x * v - a.y * startU) * invDet;
			const Vector3 row = p + e * s0 + f * t0;
			auto* pixel = mask.data() + (size_t(y) * resolution + x0) * 4 + channel;
			uint32_t first = UINT32_MAX, last = 0;
			for (int x = x0; x <= x1; ++x, pixel += 4) {
				const auto i = static_cast<float>(x - x0);
				const float su = s0 + stepS * i, tv = t0 + stepT * i;
				if (su < 0 || tv < 0 || 1 < su + tv) continue;
				const Vector3 point(row.x + stepPoint.x * i, row.y + stepPoint.y * i, row.z + stepPoint.z * i);
				const Vector3 offset((point.x - center.x) * invRadius, (point.y - center.y) * invRadius, (point.z - center.z) * invRadius);
				const float squared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
				if (1 <= squared || (erase ? *pixel == 0 : *pixel == 255) || !surface.Visible(point, face)) continue;
				const float fade = std::clamp((1 - std::sqrt(squared)) * invFeather, 0.0f, 1.0f);
				float weight = strength * fade * fade * (3 - 2 * fade);
				if (!shape.empty()) {
					const float tx = Vector3::Dot(offset, tangent), ty = Vector3::Dot(offset, bitangent);
					const auto sx = std::min(shapeWidth - 1, uint32_t(std::max(0.0f, 0.5f + 0.5f * (cs * tx + sn * ty)) * shapeWidth));
					const auto sy = std::min(shapeHeight - 1, uint32_t(std::max(0.0f, 0.5f + 0.5f * (-sn * tx + cs * ty)) * shapeHeight));
					weight *= shape[size_t(sy) * shapeWidth + sx] / 255.0f;
				}
				auto& value = *pixel;
				const auto next = static_cast<uint8_t>(std::lround(std::lerp(float(value), erase ? 0.0f : 255.0f, weight)));
				if (value == next) continue;
				changed = true;
				first = std::min(first, uint32_t(x));
				last = uint32_t(x + 1);
				value = next;
			}
			if (dirty && last) {
				dirty->left = std::min(dirty->left, first);
				dirty->right = std::max(dirty->right, last);
				dirty->top = std::min(dirty->top, uint32_t(y));
				dirty->bottom = std::max(dirty->bottom, uint32_t(y + 1));
			}
		}
		return changed;
	}
}
