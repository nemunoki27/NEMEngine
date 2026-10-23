#include "ParticleStorage.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <stdexcept>

using namespace Engine;

void ParticleStorage::Configure(uint32_t stateCount) {

	if (size_ != 0 && stateCount != stateCount_) throw std::logic_error("粒子が生存中のModule状態は変更できません");
	stateCount_ = stateCount;
}

void ParticleStorage::Reserve(size_t count) {

	if (stateCount_ && count > moduleStates_.max_size() / stateCount_) throw std::length_error("粒子のModule状態が大きすぎます");
	columns_.Reserve(count);
	moduleStates_.reserve(count * stateCount_);
}

void ParticleStorage::Append(const Particle& value) {

	// 評価用Spanを保存せず、その値だけを任意状態へ移す
	columns_.Resize(size_ + 1);
	moduleStates_.resize((size_ + 1) * stateCount_);
	columns_.Set(size_, value);
	if (!value.rotationSpeeds.empty()) {
		std::copy_n(value.rotationSpeeds.begin(), (std::min)(value.rotationSpeeds.size(), size_t(stateCount_)),
			moduleStates_.begin() + size_ * stateCount_);
	}
	++size_;
}

void ParticleStorage::RemoveSwap(size_t index) {

	const size_t last = size_ - 1;
	if (index != last) {
		columns_.Set(index, columns_.Get(last));
		std::copy_n(moduleStates_.begin() + last * stateCount_, stateCount_, moduleStates_.begin() + index * stateCount_);
	}
	columns_.Resize(last);
	moduleStates_.resize(last * stateCount_);
	size_ = last;
}

void ParticleStorage::Set(size_t index, const Particle& value) {

	columns_.Set(index, value);
}

void ParticleStorage::clear() {

	columns_.Resize(0);
	moduleStates_.clear();
	size_ = 0;
}

Particle ParticleStorage::operator[](size_t index) const {

	// 描画からModule状態を書き換えられるSpanを公開しない
	return columns_.Get(index);
}

Particle ParticleStorage::GetMutable(size_t index) {

	Particle value = columns_.Get(index);
	if (stateCount_) value.rotationSpeeds = std::span(moduleStates_).subspan(index * stateCount_, stateCount_);
	return value;
}
