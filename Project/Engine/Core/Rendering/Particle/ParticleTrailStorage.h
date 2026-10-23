#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleTypes.h"
#include "ParticleSystemSettings.h"
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

namespace Engine {

	// Trailは粒子スロットとは独立した世代で参照する
	struct ParticleTrailStorageTag;
	using ParticleTrailHandle = StorageHandle<ParticleTrailStorageTag>;

	struct ParticleTrailRuntime {

		ParticleTrailPoint head;
		uint32_t pointSlot = 0;
		uint32_t begin = 0;
		uint32_t count = 0;
		uint64_t lastTouched = 0;
		bool detached = false;
	};

	//============================================================================
	//	ParticleTrailStorage class
	//	Trailの世代付き所有と共有リング領域を管理する
	//============================================================================
	class ParticleTrailStorage {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// メモリ上限から実際のTrail数を確定する
		void Configure(const ParticleTrailSettings& settings);
		// 空きがある場合だけ新しいTrailを確保する
		ParticleTrailHandle Allocate();
		// 点列を解放してスロットを再利用可能にする
		void Release(ParticleTrailHandle handle);
		// 移動距離を満たした点をリングへ追加する
		void Record(ParticleTrailRuntime& trail, const ParticleTrailPoint& point, float minimumDistance);
		// 期限切れの先頭点を除く
		void Expire(ParticleTrailRuntime& trail, float deltaTime, float lifetime);
		// 全Trailを消す
		void clear();

		//--------- accessor -----------------------------------------------------

		ParticleTrailRuntime* TryGet(ParticleTrailHandle handle);
		const ParticleTrailRuntime* TryGet(ParticleTrailHandle handle) const;
		ParticleTrailPoint& GetPoint(ParticleTrailRuntime& trail, uint32_t index);
		const ParticleTrailPoint& GetPoint(const ParticleTrailRuntime& trail, uint32_t index) const;
		const std::vector<ParticleTrailHandle>& GetHandles() const { return handles_; }
		uint32_t GetCapacity() const { return capacity_; }
		uint32_t GetPointCapacity() const { return pointCapacity_; }
		uint64_t GetMemoryBytes() const { return uint64_t(points_.size()) * sizeof(ParticleTrailPoint); }
		uint64_t GetOverflowCount() const { return overflowCount_; }
		bool empty() const { return handles_.empty(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		using Pool = GenerationalPool<ParticleTrailRuntime, ParticleTrailStorageTag>;
		std::unique_ptr<Pool> pool_;
		std::vector<ParticleTrailHandle> handles_;
		std::vector<uint32_t> freePointSlots_;
		std::vector<ParticleTrailPoint> points_;
		uint64_t overflowCount_ = 0;
		uint32_t capacity_ = 0;
		uint32_t pointCapacity_ = 0;
	};
}
