#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleSimulationCheckpoint.h"
#include "ParticleInstanceUpdater.h"

// c++
#include <functional>

namespace Engine {

	//============================================================================
	//	ParticleSimulationReplay class
	//	固定刻みと上限付きの途中状態から任意時刻の粒子を再現する
	//============================================================================
	class ParticleSimulationReplay {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using TransformSampler = std::function<bool(double, Matrix4x4&)>;
		static constexpr double kFixedDelta = 1.0 / 60.0;
		static constexpr size_t kMaxCheckpoints = 8;
		static constexpr uint64_t kMemoryBudget = 64ull * 1024 * 1024;

		void Reset(uint32_t seed, bool oneShot);
		// 準備途中の結果を公開せず、要求時刻まで同じ呼出し内で評価する
		void Evaluate(ECSWorld& world, ParticleInstanceState& instance,
			std::shared_ptr<const ParticleSystemSettings> settings, ParticleSimulationTarget target,
			const ParticlePhaseParentSettings& parent, bool phaseParent, double time,
			uint64_t revision, bool suppressFeedback, const TransformSampler& sampleTransform);
		size_t GetCheckpointCount() const { return checkpoints_.size() + (boundary_ ? 1u : 0u); }
		uint64_t GetCheckpointMemoryBytes() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct Checkpoint {

			uint64_t tick = 0;
			std::unique_ptr<ParticleSimulationCheckpoint> state;
		};
		ParticleInstanceUpdater updater_;
		std::vector<Checkpoint> checkpoints_;
		std::unique_ptr<Checkpoint> boundary_;
		std::shared_ptr<const ParticleSystemSettings> settings_;
		uint64_t revision_ = 0;
		uint64_t tick_ = 0;
		uint64_t feedbackTick_ = 0;
		uint32_t seed_ = 1;
		uint8_t parentSignature_ = 0;
		bool oneShot_ = false;
		bool initialized_ = false;
		bool partial_ = false;
		bool feedbackInitialized_ = false;
		double time_ = -1;

		void SaveCheckpoint(ParticleInstanceState& instance, bool boundary);
		void Restore(ParticleInstanceState& instance, uint64_t tick);
		void TrimCheckpoints();
	};
}
