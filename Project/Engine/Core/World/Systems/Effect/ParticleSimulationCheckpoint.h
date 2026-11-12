#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>

namespace Engine {

	//============================================================================
	//	ParticleSimulationCheckpoint class
	//	粒子と乱数列、Trailの独立した途中状態を保持する
	//============================================================================
	class ParticleSimulationCheckpoint {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// CPUの状態を複製し、GPUには同じ更新位置での複製を要求する
		explicit ParticleSimulationCheckpoint(ParticleInstanceState& instance);
		// 描画側の複製が利用可能になったか
		bool CanRestore() const;
		// 同じGPU所有者を維持して保存位置へ戻す
		bool Restore(ParticleInstanceState& instance) const;
		uint64_t GetMemoryBytes() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ParticleInstanceState state_;
		std::shared_ptr<ParticleGPUCheckpoint> gpuCheckpoint_;
		std::weak_ptr<ParticleGPUState> gpuOwner_;
		std::vector<ParticleParentGPU> gpuParents_;
		uint64_t emissionEndSequence_ = UINT64_MAX;
	};

	// 資源の所有を維持したまま乱数列と再生位置を初期化する
	void ResetParticleSimulation(ParticleInstanceState& instance, uint32_t seed, bool oneShot);
}
