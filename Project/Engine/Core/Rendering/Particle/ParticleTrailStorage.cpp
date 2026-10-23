#include "ParticleTrailStorage.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

using namespace Engine;

void ParticleTrailStorage::Configure(const ParticleTrailSettings& settings) {

	const uint32_t points = static_cast<uint32_t>(std::clamp(settings.maxPoints, 2, 4096));
	const uint64_t budget = uint64_t(settings.memoryBudgetMB) * 1024 * 1024;
	const uint64_t requested = settings.mode == ParticleTrailMode::Ribbon ? 1u : settings.maxTrails;
	const uint32_t trails = static_cast<uint32_t>((std::min)(requested, budget / (points * sizeof(ParticleTrailPoint))));
	if (capacity_ == trails && pointCapacity_ == points) return;
	clear();
	capacity_ = trails;
	pointCapacity_ = points;
	points_.resize(size_t(trails) * points);
	freePointSlots_.clear();
	for (uint32_t i = 0; i < trails; ++i) freePointSlots_.push_back(i);
	handles_.reserve(trails);
	if (!pool_) pool_ = std::make_unique<Pool>();
}

ParticleTrailHandle ParticleTrailStorage::Allocate() {

	// 上限では既存の尾を維持し、新しいTrailだけを見送る
	if (!pool_ || handles_.size() >= capacity_) {
		++overflowCount_;
		return ParticleTrailHandle::Null();
	}
	const auto handle = pool_->Emplace();
	pool_->TryGet(handle)->pointSlot = freePointSlots_.back();
	freePointSlots_.pop_back();
	handles_.push_back(handle);
	return handle;
}

void ParticleTrailStorage::Release(ParticleTrailHandle handle) {

	if (auto* trail = TryGet(handle)) {
		const uint32_t slot = trail->pointSlot;
		pool_->Release(handle);
		freePointSlots_.push_back(slot);
		std::erase(handles_, handle);
	}
}

void ParticleTrailStorage::Record(ParticleTrailRuntime& trail, const ParticleTrailPoint& point, float minimumDistance) {

	trail.head = point;
	if (trail.count) {
		const auto delta = point.position - GetPoint(trail, trail.count - 1).position;
		if (Vector3::Dot(delta, delta) < minimumDistance * minimumDistance) return;
	}
	if (trail.count == pointCapacity_) {
		trail.begin = (trail.begin + 1) % pointCapacity_;
		--trail.count;
	}
	GetPoint(trail, trail.count++) = point;
}

void ParticleTrailStorage::Expire(ParticleTrailRuntime& trail, float deltaTime, float lifetime) {

	for (uint32_t i = 0; i < trail.count; ++i) GetPoint(trail, i).age += deltaTime;
	if (trail.detached) trail.head.age += deltaTime;
	while (trail.count && GetPoint(trail, 0).age >= lifetime) {
		trail.begin = (trail.begin + 1) % pointCapacity_;
		--trail.count;
	}
}

void ParticleTrailStorage::clear() {

	if (pool_) pool_->Clear();
	handles_.clear();
	overflowCount_ = 0;
	freePointSlots_.clear();
	for (uint32_t i = 0; i < capacity_; ++i) freePointSlots_.push_back(i);
}

ParticleTrailRuntime* ParticleTrailStorage::TryGet(ParticleTrailHandle handle) {

	return pool_ ? pool_->TryGet(handle) : nullptr;
}

const ParticleTrailRuntime* ParticleTrailStorage::TryGet(ParticleTrailHandle handle) const {

	return pool_ ? pool_->TryGet(handle) : nullptr;
}

ParticleTrailPoint& ParticleTrailStorage::GetPoint(ParticleTrailRuntime& trail, uint32_t index) {

	return points_[size_t(trail.pointSlot) * pointCapacity_ + (trail.begin + index) % pointCapacity_];
}

const ParticleTrailPoint& ParticleTrailStorage::GetPoint(const ParticleTrailRuntime& trail, uint32_t index) const {

	return points_[size_t(trail.pointSlot) * pointCapacity_ + (trail.begin + index) % pointCapacity_];
}
