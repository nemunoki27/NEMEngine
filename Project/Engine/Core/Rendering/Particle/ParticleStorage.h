#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleTypes.h"
#include <Engine/Core/Foundation/Utility/StructureOfArrays.h>

namespace Engine {

	//============================================================================
	//	ParticleStorage class
	//	粒子の属性列とModuleの任意状態をチャンク外で保持する
	//============================================================================
	class ParticleStorage {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 読取時だけ属性列から一時値を組み立てる
		class Iterator {
		public:
			Iterator(const ParticleStorage* owner, size_t index) : owner_(owner), index_(index) {}
			Particle operator*() const { return (*owner_)[index_]; }
			Iterator& operator++() { ++index_; return *this; }
			bool operator!=(const Iterator& other) const { return index_ != other.index_; }
		private:
			const ParticleStorage* owner_;
			size_t index_;
		};

		// 再生開始前に必要なModule状態数を決める
		void Configure(uint32_t stateCount);
		// 容量をまとめて確保する
		void Reserve(size_t count);
		// 粒子を追加する
		void Append(const Particle& value);
		// 末尾粒子と入れ替えて削除する
		void RemoveSwap(size_t index);
		// 評価結果を保存する
		void Set(size_t index, const Particle& value);
		// 使用中の要素だけを消去する
		void clear();

		//--------- accessor -----------------------------------------------------

		static constexpr size_t GetAttributeBytesPerParticle() { return decltype(columns_)::kElementBytes; }
		Particle operator[](size_t index) const;
		Particle GetMutable(size_t index);
		size_t size() const { return size_; }
		bool empty() const { return size_ == 0; }
		Iterator begin() const { return Iterator(this, 0); }
		Iterator end() const { return Iterator(this, size_); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructureOfArrays<Particle, &Particle::pos, &Particle::velocity, &Particle::spawnDirection,
			&Particle::age, &Particle::previousAge, &Particle::lifetime, &Particle::size, &Particle::scale,
			&Particle::color, &Particle::emissive, &Particle::alphaReference, &Particle::rotation,
			&Particle::uvScale, &Particle::uvOffset, &Particle::uvRotation, &Particle::uvPivot,
			&Particle::id, &Particle::trailIndex, &Particle::trailGeneration, &Particle::phaseIndex, &Particle::previousPhaseIndex,
			&Particle::worldPos, &Particle::worldRotation, &Particle::worldScale,
			&Particle::parentMatrix, &Particle::hasParent, &Particle::shapeData> columns_;
		std::vector<Vector3> moduleStates_;
		size_t size_ = 0;
		uint32_t stateCount_ = 0;
	};
}
