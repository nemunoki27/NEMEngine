#include "MeshSurfaceGeometry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <DirectXCollision.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {
	using namespace Engine;

	float Coordinate(const Vector3& value, uint32_t axis) {
		return axis == 0u ? value.x : axis == 1u ? value.y : value.z;
	}

	bool InRange(const Vector3& p, float radius, const Vector3& lo, const Vector3& hi) {
		const float x = std::max({lo.x - p.x, 0.0f, p.x - hi.x});
		const float y = std::max({lo.y - p.y, 0.0f, p.y - hi.y});
		const float z = std::max({lo.z - p.z, 0.0f, p.z - hi.z});
		return x * x + y * y + z * z <= radius * radius;
	}
}

std::shared_ptr<const Engine::MeshSurfaceGeometry> Engine::MeshSurfaceGeometry::Build(const ImportedMeshAsset& mesh) {

	if (mesh.subMeshes.empty() || mesh.indices.empty() || mesh.vertices.empty()) return {};
	auto result = std::make_shared<MeshSurfaceGeometry>();
	if (mesh.isSkinned) return result->BuildJointBounds(mesh) ? result : nullptr;
	result->positions_.reserve(mesh.vertices.size());
	for (const auto& vertex : mesh.vertices) {
		const auto& p = vertex.position;
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
		result->positions_.emplace_back(p.x, p.y, p.z);
	}
	result->triangles_.reserve(mesh.indices.size() / 3);
	for (const auto& sub : mesh.subMeshes) {
		if (sub.indexCount % 3u || sub.indexOffset > mesh.indices.size() || sub.indexCount > mesh.indices.size() - sub.indexOffset) return {};
		const auto first = static_cast<uint32_t>(result->triangles_.size());
		for (uint32_t i = 0; i < sub.indexCount; i += 3u) {
			const auto* ix = mesh.indices.data() + sub.indexOffset + i;
			if (std::max({ix[0], ix[1], ix[2]}) >= mesh.vertices.size()) return {};
			result->triangles_.push_back({ix[0], ix[1], ix[2]});
		}
		result->ranges_.emplace_back(first, sub.indexCount / 3u);
	}
	return result;
}

bool Engine::MeshSurfaceGeometry::BuildJointBounds(const ImportedMeshAsset& mesh) {

	if (!mesh.boneCount || mesh.vertexInfluences.size() != mesh.vertices.size()) return false;
	std::unordered_map<uint32_t, uint32_t> slots;
	for (const auto& sub : mesh.subMeshes) {
		if (sub.indexOffset > mesh.indices.size() || sub.indexCount > mesh.indices.size() - sub.indexOffset) return false;
		const auto first = static_cast<uint32_t>(jointBounds_.size());
		slots.clear();
		const auto include = [&](uint32_t joint, const Vector3& p) {
			const auto [it, added] = slots.try_emplace(joint, static_cast<uint32_t>(jointBounds_.size()));
			if (added) {
				jointBounds_.push_back({p, p, joint});
				return;
			}
			auto& b = jointBounds_[it->second];
			b.minimum = {std::min(b.minimum.x, p.x), std::min(b.minimum.y, p.y), std::min(b.minimum.z, p.z)};
			b.maximum = {std::max(b.maximum.x, p.x), std::max(b.maximum.y, p.y), std::max(b.maximum.z, p.z)};
		};
		for (const uint32_t index : std::span(mesh.indices).subspan(sub.indexOffset, sub.indexCount)) {
			if (index >= mesh.vertices.size()) return false;
			const auto& v = mesh.vertices[index].position;
			if (v.w != 1.0f || !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
			const Vector3 p{v.x, v.y, v.z};
			const auto& influence = mesh.vertexInfluences[index];
			float sum = 0.0f;
			for (uint32_t j = 0; j < kNumMaxInfluence; ++j) {
				const float weight = influence.weights[j];
				const int32_t joint = influence.jointIndices[j];
				if (!std::isfinite(weight) || weight < 0.0f) return false;
				if (weight == 0.0f || joint < 0) continue;
				if (uint32_t(joint) >= mesh.boneCount || !std::isnormal(weight)) return false;
				include(uint32_t(joint), p);
				sum += weight;
			}
			if (!std::isfinite(sum)) return false;
			if (sum == 0.0f) include(UINT32_MAX, p);
		}
		ranges_.emplace_back(first, static_cast<uint32_t>(jointBounds_.size()) - first);
	}
	return !jointBounds_.empty();
}

Engine::MeshSurfaceGeometry::PoseBounds Engine::MeshSurfaceGeometry::GetSkinnedBounds(uint32_t subMesh, std::span<const WellForGPU> palette) const {

	const PoseBounds unknown{};
	if (jointBounds_.empty() || subMesh >= ranges_.size() || !ranges_[subMesh].second) return unknown;
	const auto [first, count] = ranges_[subMesh];
	std::array<double, 3> lo, hi;
	lo.fill(std::numeric_limits<double>::max());
	hi.fill(std::numeric_limits<double>::lowest());
	// スキニングは正のウェイトを正規化するため、変換した境界すべての凸包に収まる
	for (const auto& joint : std::span(jointBounds_).subspan(first, count)) {
		const std::array<double, 3> a{joint.minimum.x, joint.minimum.y, joint.minimum.z};
		const std::array<double, 3> b{joint.maximum.x, joint.maximum.y, joint.maximum.z};
		if (joint.joint != UINT32_MAX && joint.joint >= palette.size()) return unknown;
		for (uint32_t axis = 0; axis < 3; ++axis) {
			double lower = a[axis], upper = b[axis];
			if (joint.joint != UINT32_MAX) {
				const auto& m = palette[joint.joint].skeletonSpaceMatrix;
				lower = upper = m.m[3][axis];
				double magnitude = std::abs(lower);
				for (uint32_t j = 0; j < 3; ++j) {
					const double x = a[j] * m.m[j][axis], y = b[j] * m.m[j][axis];
					lower += std::min(x, y);
					upper += std::max(x, y);
					magnitude += std::max(std::abs(x), std::abs(y));
				}
				const double margin = magnitude * (16.0 * std::numeric_limits<float>::epsilon());
				lower -= margin;
				upper += margin;
			}
			if (!std::isfinite(lower) || !std::isfinite(upper)) return unknown;
			lo[axis] = std::min(lo[axis], lower);
			hi[axis] = std::max(hi[axis], upper);
		}
	}
	std::array<float, 3> center{};
	std::array<double, 3> extent{};
	for (uint32_t axis = 0; axis < 3; ++axis) {
		center[axis] = static_cast<float>((lo[axis] + hi[axis]) * 0.5);
		extent[axis] = std::max(hi[axis] - center[axis], center[axis] - lo[axis]);
	}
	const float radius = std::nextafter(static_cast<float>(std::hypot(extent[0], extent[1], extent[2])),
		std::numeric_limits<float>::infinity());
	if (!std::isfinite(radius)) return unknown;
	Vector3 halfExtent{
		std::nextafter(float(extent[0]), std::numeric_limits<float>::infinity()),
		std::nextafter(float(extent[1]), std::numeric_limits<float>::infinity()),
		std::nextafter(float(extent[2]), std::numeric_limits<float>::infinity())};
	return {{center[0], center[1], center[2], radius}, halfExtent};
}

void Engine::MeshSurfaceGeometry::BuildHierarchy() const {

	nodes_.clear();
	roots_.clear();
	roots_.reserve(ranges_.size());
	for (const auto& [first, count] : ranges_) {
		if (count == 0) {
			roots_.push_back(UINT32_MAX);
			continue;
		}
		const auto root = static_cast<uint32_t>(nodes_.size());
		roots_.push_back(root);
		nodes_.emplace_back();
		BuildNode(root, first, count);
	}
}

void Engine::MeshSurfaceGeometry::BuildNode(uint32_t node, uint32_t first, uint32_t count) const {

	auto lo = positions_[triangles_[first].indices[0]], hi = lo;
	for (uint32_t i = first; i < first + count; ++i) {
		for (const auto index : triangles_[i].indices) {
			const auto& v = positions_[index];
			lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
			hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
		}
	}
	nodes_[node] = {lo, hi, first, count};
	if (count <= 16u) return;
	const auto size = hi - lo;
	const uint32_t axis = size.x >= size.y && size.x >= size.z ? 0u : size.y >= size.z ? 1u : 2u;
	const auto half = count / 2u;
	auto center = [&](const Triangle& tri) {
		return Coordinate(positions_[tri.indices[0]], axis) + Coordinate(positions_[tri.indices[1]], axis) + Coordinate(positions_[tri.indices[2]], axis);
	};
	std::nth_element(triangles_.begin() + first, triangles_.begin() + first + half, triangles_.begin() + first + count,
		[&](const auto& a, const auto& b) { return center(a) < center(b); });
	const auto child = static_cast<uint32_t>(nodes_.size());
	nodes_[node].first = child;
	nodes_[node].count = 0;
	nodes_.resize(child + 2u);
	BuildNode(child, first, half);
	BuildNode(child + 1u, first + half, count - half);
}

bool Engine::MeshSurfaceGeometry::IntersectsSphere(uint32_t subMesh, const Vector3& center, float radius) const {

	if (!jointBounds_.empty() || subMesh >= ranges_.size()) return true;
	// GIを使うときだけ、背景ワーカー上で三角形の階層を構築する
	std::call_once(hierarchyReady_, [this] { BuildHierarchy(); });
	const auto root = roots_[subMesh];
	if (root == UINT32_MAX) return false;
	const DirectX::BoundingSphere sphere({center.x, center.y, center.z}, radius);
	auto vertex = [&](uint32_t index) {
		const auto& p = positions_[index];
		return DirectX::XMVectorSet(p.x, p.y, p.z, 1.0f);
	};
	std::array<uint32_t, 32> stack{};
	uint32_t pending = 1;
	stack[0] = root;
	while (pending) {
		const auto& node = nodes_[stack[--pending]];
		if (!InRange(center, radius, node.minimum, node.maximum)) continue;
		if (node.count == 0) {
			stack[pending++] = node.first;
			stack[pending++] = node.first + 1u;
			continue;
		}
		for (uint32_t i = node.first; i < node.first + node.count; ++i) {
			const auto& ix = triangles_[i].indices;
			const auto a = vertex(ix[0]), b = vertex(ix[1]), c = vertex(ix[2]);
			const auto n = DirectX::XMVector3Cross(DirectX::XMVectorSubtract(b, a), DirectX::XMVectorSubtract(c, a));
			// 面積ゼロの三角形は上の境界判定を保守的な結果として使う
			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) <= 1e-20f || sphere.Intersects(a, b, c)) return true;
		}
	}
	return false;
}
