#include "DecalProjection.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

bool Engine::DecalProjectionUtility::Build(
	const DecalProjectorComponent& component, const Matrix4x4& world, DecalProjection& output) {

	// 逆変換できない寸法を描画へ渡さない
	if (!std::isfinite(component.size.x) || !std::isfinite(component.size.y) || !std::isfinite(component.size.z) ||
		component.size.x <= 0.0f || component.size.y <= 0.0f || component.size.z <= 0.0f) return false;
	DecalProjection projection;
	projection.decalToWorld = Matrix4x4::MakeScaleMatrix(component.size) *
		Matrix4x4::MakeTranslateMatrix(component.pivot) * world;
	for (const auto& row : projection.decalToWorld.m) {
		for (float value : row) if (!std::isfinite(value)) return false;
	}
	if (!Matrix4x4::TryInverse(projection.decalToWorld, projection.worldToDecal)) return false;
	projection.center = Vector3::Transform(Vector3{}, projection.decalToWorld);
	output = projection;
	return true;
}

bool Engine::DecalProjectionUtility::Intersects(const Matrix4x4& worldToDecal, const Vector3& center, float radius) {

	// 逆行列の各列で球の最大到達距離を求める
	const Vector3 local = Vector3::Transform(center, worldToDecal);
	const float values[]{local.x, local.y, local.z};
	for (uint32_t axis = 0; axis < 3; ++axis) {
		const float scale = std::sqrt(worldToDecal.m[0][axis] * worldToDecal.m[0][axis] +
			worldToDecal.m[1][axis] * worldToDecal.m[1][axis] + worldToDecal.m[2][axis] * worldToDecal.m[2][axis]);
		if (std::abs(values[axis]) > 0.5f + radius * scale) return false;
	}
	return true;
}

bool Engine::DecalProjectionUtility::GetScreenRect(const DecalProjection& projection, const Matrix4x4& viewProjection,
	uint32_t width, uint32_t height, int32_t& left, int32_t& top, int32_t& right, int32_t& bottom) {

	// 8隅の射影から処理する画素範囲を求める
	const Matrix4x4 matrix = projection.decalToWorld * viewProjection;
	float minX = 1.0f, minY = 1.0f, maxX = -1.0f, maxY = -1.0f;
	uint32_t visibleCorners = 0;
	for (uint32_t corner = 0; corner < 8; ++corner) {
		const float x = (corner & 1u) ? 0.5f : -0.5f;
		const float y = (corner & 2u) ? 0.5f : -0.5f;
		const float z = (corner & 4u) ? 0.5f : -0.5f;
		const float w = x * matrix.m[0][3] + y * matrix.m[1][3] + z * matrix.m[2][3] + matrix.m[3][3];
		const float clipZ = x * matrix.m[0][2] + y * matrix.m[1][2] + z * matrix.m[2][2] + matrix.m[3][2];
		if (w <= 1e-6f || clipZ <= 0.0f) {
			continue;
		}
		++visibleCorners;
		const float clipX = (x * matrix.m[0][0] + y * matrix.m[1][0] + z * matrix.m[2][0] + matrix.m[3][0]) / w;
		const float clipY = (x * matrix.m[0][1] + y * matrix.m[1][1] + z * matrix.m[2][1] + matrix.m[3][1]) / w;
		minX = std::min(minX, clipX); maxX = std::max(maxX, clipX);
		minY = std::min(minY, clipY); maxY = std::max(maxY, clipY);
	}
	if (visibleCorners == 0) return false;
	if (visibleCorners < 8) {
		// Near面と交差するときだけ画面全体を確認する
		left = top = 0;
		right = static_cast<int32_t>(width);
		bottom = static_cast<int32_t>(height);
		return true;
	}
	left = static_cast<int32_t>(std::floor(std::clamp(minX * 0.5f + 0.5f, 0.0f, 1.0f) * width));
	right = static_cast<int32_t>(std::ceil(std::clamp(maxX * 0.5f + 0.5f, 0.0f, 1.0f) * width));
	top = static_cast<int32_t>(std::floor(std::clamp(0.5f - maxY * 0.5f, 0.0f, 1.0f) * height));
	bottom = static_cast<int32_t>(std::ceil(std::clamp(0.5f - minY * 0.5f, 0.0f, 1.0f) * height));
	return left < right && top < bottom;
}

float Engine::DecalProjectionUtility::DistanceFade(float distance, float drawDistance, float fadeScale) {

	if (!std::isfinite(distance) || !std::isfinite(drawDistance) || drawDistance <= 0.0f || distance >= drawDistance) return 0.0f;
	const float start = drawDistance * (std::isfinite(fadeScale) ? std::clamp(fadeScale, 0.0f, 1.0f) : 0.9f);
	return distance <= start ? 1.0f : (drawDistance - distance) / (drawDistance - start);
}
