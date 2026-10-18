#include "MeshBoundsUtility.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

void Engine::MeshBoundsUtility::Calculate(std::span<const MeshVertex> vertices,
	std::span<const uint32_t> indices, Vector3& center, float& radius) {

	center = Vector3{};
	radius = 0.0f;
	if (vertices.empty()) return;
	const size_t count = indices.empty() ? vertices.size() : indices.size();
	const auto position = [&](size_t index) {
		const auto& value = vertices[indices.empty() ? index : indices[index]].position;
		return Vector3(value.x, value.y, value.z);
	};
	// 同じ頂点列からMesh全体とSubMeshのBoundsを求める
	Vector3 minimum = position(0), maximum = minimum;
	for (size_t index = 1; index < count; ++index) {
		const Vector3 value = position(index);
		minimum.x = std::min(minimum.x, value.x); maximum.x = std::max(maximum.x, value.x);
		minimum.y = std::min(minimum.y, value.y); maximum.y = std::max(maximum.y, value.y);
		minimum.z = std::min(minimum.z, value.z); maximum.z = std::max(maximum.z, value.z);
	}
	center = (minimum + maximum) * 0.5f;
	for (size_t index = 0; index < count; ++index) radius = std::max(radius, (position(index) - center).Length());
}

void Engine::MeshBoundsUtility::TransformSphere(const Matrix4x4& matrix, Vector3& center, float& radius) {

	center = Vector3::Transform(center, matrix);
	// A・A転置の行和で最大拡大率の上限を求める
	float maximum = 0.0f;
	for (uint32_t row = 0; row < 3; ++row) {
		float sum = 0.0f;
		for (uint32_t column = 0; column < 3; ++column) {
			float value = 0.0f;
			for (uint32_t axis = 0; axis < 3; ++axis) value += matrix.m[row][axis] * matrix.m[column][axis];
			sum += std::abs(value);
		}
		maximum = std::max(maximum, sum);
	}
	radius *= std::sqrt(maximum);
}
