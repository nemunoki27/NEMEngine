#include "ParticleGPUWorldInputs.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <numeric>

using namespace Engine;

namespace {

	struct Bounds {

		Vector3 minimum{};
		Vector3 maximum{};
	};

	// 各軸の小さい成分を返す
	Vector3 Minimum(Vector3 a, Vector3 b) {

		return Vector3((std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z));
	}

	// 各軸の大きい成分を返す
	Vector3 Maximum(Vector3 a, Vector3 b) {

		return Vector3((std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z));
	}

	// 回転中の形状も含む保守的な移動境界を作る
	Bounds MakeBounds(const ParticleGPUCollider& collider, float deltaTime) {

		if ((collider.metadata[0] & 0x7fffffffu) == 0) return {Vector3::AnyInit(-1e20f), Vector3::AnyInit(1e20f)};
		const Vector3 center(collider.centerRadius.x, collider.centerRadius.y, collider.centerRadius.z);
		float radius = collider.centerRadius.w;
		if ((collider.metadata[0] & 0x7fffffffu) == 2) radius = Vector3(collider.axisXExtent.w, collider.axisYExtent.w, collider.axisZExtent.w).Length();
		if ((collider.metadata[0] & 0x7fffffffu) == 3) radius += Vector3(collider.segmentEnd.x - collider.segmentStart.x,
			collider.segmentEnd.y - collider.segmentStart.y, collider.segmentEnd.z - collider.segmentStart.z).Length() * 0.5f;
		const auto previous = center - Vector3(collider.velocity.x, collider.velocity.y, collider.velocity.z) * deltaTime;
		return {Minimum(center, previous) - radius, Maximum(center, previous) + radius};
	}

	// 中央値分割し、枝を飛び越す位置を深さ優先順に記録する
	void BuildNodes(std::span<uint32_t> indices, const std::vector<Bounds>& bounds, std::vector<ParticleGPUCollisionNode>& nodes) {

		const auto index = static_cast<uint32_t>(nodes.size());
		ParticleGPUCollisionNode node;
		node.minimum = bounds[indices.front()].minimum;
		node.maximum = bounds[indices.front()].maximum;
		for (uint32_t collider : indices) {
			node.minimum = Minimum(node.minimum, bounds[collider].minimum);
			node.maximum = Maximum(node.maximum, bounds[collider].maximum);
		}
		nodes.push_back(node);
		if (indices.size() == 1) nodes[index].collider = indices.front();
		else {
			const auto size = node.maximum - node.minimum;
			const uint32_t axis = size.x > size.y ? (size.x > size.z ? 0 : 2) : (size.y > size.z ? 1 : 2);
			const auto middle = indices.begin() + indices.size() / 2;
			std::nth_element(indices.begin(), middle, indices.end(), [&](uint32_t a, uint32_t b) {
				const auto ca = bounds[a].minimum + bounds[a].maximum;
				const auto cb = bounds[b].minimum + bounds[b].maximum;
				return axis == 0 ? ca.x < cb.x : axis == 1 ? ca.y < cb.y : ca.z < cb.z;
			});
			BuildNodes(indices.first(indices.size() / 2), bounds, nodes);
			BuildNodes(indices.subspan(indices.size() / 2), bounds, nodes);
		}
		nodes[index].escape = static_cast<uint32_t>(nodes.size());
	}
}

void Engine::BuildParticleGPUCollisionBVH(ParticleGPUWorldSnapshot& snapshot, float deltaTime) {

	snapshot.collisionNodes.clear();
	if (snapshot.colliders.empty()) return;
	std::vector<Bounds> bounds;
	bounds.reserve(snapshot.colliders.size());
	for (const auto& collider : snapshot.colliders) bounds.push_back(MakeBounds(collider, deltaTime));
	std::vector<uint32_t> indices(snapshot.colliders.size());
	std::iota(indices.begin(), indices.end(), 0u);
	snapshot.collisionNodes.reserve(indices.size() * 2 - 1);
	BuildNodes(indices, bounds, snapshot.collisionNodes);
}
