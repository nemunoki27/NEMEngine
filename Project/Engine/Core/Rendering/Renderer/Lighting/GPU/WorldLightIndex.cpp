#include "WorldLightIndex.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace {
	using namespace Engine;

	Vector3 MinCorner(const Vector3& a, const Vector3& b) {
		return { (std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z) };
	}
	Vector3 MaxCorner(const Vector3& a, const Vector3& b) {
		return { (std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z) };
	}
}

uint32_t Engine::WorldLightIndex::Publish(GraphicsCore& graphics, const PerViewLightSet& lights, RenderBufferRegistry& registry) {

	if (!initialized_) {
		buffer_.Init(graphics.GetDXObject().GetDevice(), &graphics.GetSRVDescriptor(), &graphics.GetBufferUploadService());
		initialized_ = true;
	}
	const uint32_t count = lights.GetLocalLightCount();
	if (count >= 8) Update(lights);
	buffer_.UploadCurrentFrame(nodes_);
	registry.Register({ .alias = "gWorldLightNodes", .resource = buffer_.GetResource(),
		.gpuAddress = buffer_.GetGPUAddress(), .srvGPUHandle = buffer_.GetGPUHandle(),
		.elementCount = static_cast<uint32_t>(nodes_.size()), .stride = sizeof(Node) });
	return count >= 8 && gridActive_ ? maxCellLights_ : count;
}

void Engine::WorldLightIndex::Release() {

	buffer_.Release();
	bounds_.clear();
	spheres_.clear();
	order_.clear();
	nodes_.clear();
	ranges_.clear();
	previousRanges_.clear();
	offsets_.clear();
	indices_.clear();
	gridActive_ = false;
	initialized_ = false;
}

void Engine::WorldLightIndex::Update(const PerViewLightSet& lights) {

	const uint32_t count = lights.GetLocalLightCount();
	const bool resize = count != bounds_.size();
	bool changed = resize;
	bounds_.resize(count);
	spheres_.resize(count);
	uint32_t index = 0;
	auto append = [&](const Bounds& bounds, const Vector3& position, float radius) {
		const Vector4 sphere{position.x, position.y, position.z, (std::max)(0.0f, radius)};
		if (bounds_[index] != bounds || spheres_[index] != sphere) {
			spheres_[index] = sphere;
			bounds_[index] = bounds;
			changed = true;
		}
		++index;
	};
	auto sphere = [](Vector3 position, float radius) {
		const auto extent = Vector3::AnyInit((std::max)(radius, 0.0f));
		return Bounds{ position - extent, position + extent };
	};
	for (auto* light : lights.pointLights) append(sphere(light->pos, light->radius), light->pos, light->radius);
	for (auto* light : lights.spotLights) {
		auto bounds = sphere(light->pos, light->distance);
		const float cosine = light->cosAngle;
		const float lengthSquared = Vector3::Dot(light->direction, light->direction);
		if (0.0f < cosine && cosine < 1.0f && std::isfinite(lengthSquared) && 0.000001f < lengthSquared) {
			const auto direction = Vector3::Normalize(light->direction);
			// 外側の円錐と到達球が交わる範囲の各軸への投影
			const float sine = std::sqrt(1.0f - cosine * cosine);
			const auto extent = [&](float axis) {
				if (cosine <= axis) return light->distance;
				const float perpendicular = std::sqrt((std::max)(0.0f, 1.0f - axis * axis));
				return light->distance * std::clamp(axis * cosine + perpendicular * sine + 0.0005f, 0.0005f, 1.0f);
			};
			if (0.0f < light->distance) {
				bounds.lower = light->pos - Vector3{ extent(-direction.x), extent(-direction.y), extent(-direction.z) };
				bounds.upper = light->pos + Vector3{ extent(direction.x), extent(direction.y), extent(direction.z) };
			}
		}
		append(bounds, light->pos, light->distance);
	}
	for (auto* light : lights.rectLights) append(sphere(light->pos, light->attenuationRadius), light->pos, light->attenuationRadius);
	if (!changed) return;
	const bool wasGrid = gridActive_;
	if (count >= 512 && UpdateGrid(24u)) return;
	if (count >= 128 && UpdateGrid(16u)) return;
	gridActive_ = false;

	// 移動中は既存の木を詰め直し、境界面積が膨らんだ時だけ分割し直す
	if (resize || wasGrid || Refit() > buildCost_ * 1.75f) {
		order_.resize(count);
		std::iota(order_.begin(), order_.end(), 0u);
		nodes_.clear();
		nodes_.reserve(count * size_t{2} - 1);
		Build(0, count);
		buildCost_ = Refit();
	}
	buffer_.MarkFullUpdate(static_cast<uint32_t>(nodes_.size()));
}

bool Engine::WorldLightIndex::UpdateGrid(uint32_t side) {

	const uint32_t cellCount = side * side * side;
	Bounds domain = bounds_.front();
	for (const auto& bounds : bounds_) {
		domain.lower = MinCorner(domain.lower, bounds.lower);
		domain.upper = MaxCorner(domain.upper, bounds.upper);
	}
	const bool resize = !gridActive_ || MinCorner(domain.lower, gridBounds_.lower) != gridBounds_.lower ||
		MaxCorner(domain.upper, gridBounds_.upper) != gridBounds_.upper;
	Bounds bounds = gridBounds_;
	if (resize) {
		const Vector3 margin = MaxCorner((domain.upper - domain.lower) * 0.0625f, Vector3::AnyInit(0.01f));
		bounds = { domain.lower - margin, domain.upper + margin };
	}
	const Vector3 extent = bounds.upper - bounds.lower;
	if (!std::isfinite(extent.x) || !std::isfinite(extent.y) || !std::isfinite(extent.z)) return false;
	const Vector3 scale{ side / extent.x, side / extent.y, side / extent.z };
	const auto cell = [side](float p) {
		return static_cast<uint32_t>(std::clamp(p, 0.0f, static_cast<float>(side - 1)));
	};
	ranges_.resize(bounds_.size());
	size_t entries = 0;
	const Vector3 cellSize = extent / static_cast<float>(side);
	for (size_t i = 0; i < bounds_.size(); ++i) {
		const auto lo = bounds_[i].lower - bounds.lower;
		const auto hi = bounds_[i].upper - bounds.lower;
		auto& range = ranges_[i];
		const auto& sphere = spheres_[i];
		// 中心を含む小区画を使い、同じ区画内で移動するライトの候補表を再利用する
		range.center = { static_cast<int32_t>(std::floor((sphere.x - bounds.lower.x) * scale.x * 4.0f)),
			static_cast<int32_t>(std::floor((sphere.y - bounds.lower.y) * scale.y * 4.0f)),
			static_cast<int32_t>(std::floor((sphere.z - bounds.lower.z) * scale.z * 4.0f)) };
		range.radius = sphere.w;
		// CPUとシェーダーの丸め差がセル境界にあるライトを落とさないようにする
		constexpr float bias = 0.0001f;
		range.first = { cell(lo.x * scale.x - bias), cell(lo.y * scale.y - bias), cell(lo.z * scale.z - bias) };
		range.last = { cell(hi.x * scale.x + bias), cell(hi.y * scale.y + bias), cell(hi.z * scale.z + bias) };
		entries += size_t(range.last[0] - range.first[0] + 1) * (range.last[1] - range.first[1] + 1) *
			(range.last[2] - range.first[2] + 1);
	}
	const size_t limit = (std::min)(size_t{1} << 20, bounds_.size() * 192);
	if (entries > 2u * limit) return false;
	if (!resize && side == gridSide_ && ranges_ == previousRanges_) return true;
	auto visitCells = [&](const CellRange& range, auto&& visit) {
		const auto gap = [&](uint32_t cellIndex, int axis, float size) {
			const float quarter = range.center[axis] * 0.25f;
			return (std::max)({quarter - static_cast<float>(cellIndex + 1u) - 0.0001f,
				static_cast<float>(cellIndex) - quarter - 0.2501f, 0.0f}) * size;
		};
		const float square = range.radius * range.radius;
		for (auto z = range.first[2]; z <= range.last[2]; ++z) {
			const float dz = gap(z, 2, cellSize.z);
			for (auto y = range.first[1]; y <= range.last[1]; ++y) {
				const float dy = gap(y, 1, cellSize.y);
				const float yz = dy * dy + dz * dz;
				if (square < yz) continue;
				for (auto x = range.first[0]; x <= range.last[0]; ++x) {
					const float dx = gap(x, 0, cellSize.x);
					if (dx * dx + yz <= square) visit(x + side * (y + side * z));
				}
			}
		}
	};
	offsets_.assign(cellCount + 1, 0u);
	for (const auto& range : ranges_) visitCells(range, [&](uint32_t index) { ++offsets_[index + 1]; });
	const uint32_t maxCellLights = *std::max_element(offsets_.begin() + 1, offsets_.end());
	std::partial_sum(offsets_.begin(), offsets_.end(), offsets_.begin());
	entries = offsets_.back();
	if (entries > limit) return false;
	const size_t rangeBlocks = (offsets_.size() + 7u) / 8u;
	indices_.resize((entries + 7) & ~size_t{7});
	nodes_.resize(1 + rangeBlocks + indices_.size() / 8);
	nodes_.front() = { bounds.lower, side, scale, UINT32_MAX - 1u };
	std::memcpy(nodes_.data() + 1, offsets_.data(), offsets_.size() * sizeof(uint32_t));
	for (uint32_t i = 0; i < ranges_.size(); ++i)
		visitCells(ranges_[i], [&](uint32_t index) { indices_[offsets_[index]++] = i; });
	if (!indices_.empty()) std::memcpy(nodes_.data() + 1 + rangeBlocks, indices_.data(), indices_.size() * sizeof(uint32_t));
	gridSide_ = side;
	gridBounds_ = bounds;
	maxCellLights_ = maxCellLights;
	previousRanges_ = ranges_;
	gridActive_ = true;
	buffer_.MarkFullUpdate(static_cast<uint32_t>(nodes_.size()));
	return true;
}

uint32_t Engine::WorldLightIndex::Build(uint32_t first, uint32_t last) {

	const auto start = static_cast<uint32_t>(nodes_.size());
	auto& node = nodes_.emplace_back();
	if (last - first == 1) {
		node.light = order_[first];
	} else {
		Vector3 lower = bounds_[order_[first]].lower, upper = bounds_[order_[first]].upper;
		for (uint32_t i = first + 1; i < last; ++i) {
			lower = MinCorner(lower, bounds_[order_[i]].lower);
			upper = MaxCorner(upper, bounds_[order_[i]].upper);
		}
		const auto size = upper - lower;
		const auto center = [&](uint32_t id) {
			const auto& b = bounds_[id];
			if (size.y > size.x && size.y > size.z) return b.lower.y + b.upper.y;
			if (size.x > size.z) return b.lower.x + b.upper.x;
			return b.lower.z + b.upper.z;
		};
		const uint32_t middle = first + (last - first) / 2;
		std::nth_element(order_.begin() + first, order_.begin() + middle, order_.begin() + last,
			[&](uint32_t a, uint32_t b) { return center(a) < center(b); });
		Build(first, middle);
		Build(middle, last);
	}
	nodes_[start].next = static_cast<uint32_t>(nodes_.size());
	return start;
}

float Engine::WorldLightIndex::Refit() {

	float cost = 0.0f;
	for (size_t i = nodes_.size(); i-- > 0;) {
		auto& node = nodes_[i];
		if (node.light != UINT32_MAX) {
			node.lower = bounds_[node.light].lower;
			node.upper = bounds_[node.light].upper;
			continue;
		}
		const auto& left = nodes_[i + 1];
		const auto& right = nodes_[left.next];
		node.lower = MinCorner(left.lower, right.lower);
		node.upper = MaxCorner(left.upper, right.upper);
		const auto size = node.upper - node.lower;
		cost += size.x * size.y + size.y * size.z + size.z * size.x;
	}
	return cost;
}

