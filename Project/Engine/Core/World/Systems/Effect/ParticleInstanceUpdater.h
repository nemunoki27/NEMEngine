#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleDefinitionCompiler.h"
#include "ParticleTrailUpdater.h"

namespace Engine {

	//============================================================================
	//	ParticleInstanceUpdater class
	//	エフェクト個別再生と粒子更新の作業領域を管理する
	//============================================================================
	class ParticleInstanceUpdater {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 設定の更新を反映し、1つのエミッターを進める
		bool UpdateInstance(ECSWorld& world, ParticleInstanceState& instance,
			std::shared_ptr<const ParticleSystemSettings> settings, const Matrix4x4& emitterWorld,
			const ParticlePhaseParentSettings& parentSettings, bool usePhaseParentSettings,
			float deltaTime, bool updateSimulation, bool emissionEnabled, bool drawEmitterShape,
			ParticleSimulationTarget target = ParticleSimulationTarget::CPU);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleDefinitionCompiler definitions_;
		ParticleTrailUpdater trails_;
		std::vector<ParticleParentPose> parentRuntimes_;
		std::vector<Particle> particleScratch_;
		std::vector<size_t> particleIndices_;

		//--------- functions ----------------------------------------------------

		// 粒子とTrailを更新する
		void UpdateEmitter(const Matrix4x4& emitterWorld, ParticleEmitterRuntime& state,
			const ParticleSystemSettings& group, const ParticleSystemDefinition& runtime,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			float deltaTime, bool updateSimulation, bool emissionEnabled, bool oneShot, bool drawEmitterShape);
		// エミッター形状から発生位置と方向と初期状態を決める、firstSpawnIndexは発生順の連番の開始値
		void InitEmitterParticles(std::span<Particle> newborn, const ParticleEmitterSettings& settings,
			const ParticleValue<float>& lifetime, bool is2D, uint64_t firstSpawnIndex) const;
		// エミッター形状をデバッグ線で描画する
		void DrawEmitterShape(const Matrix4x4& emitterWorld, const ParticleEmitterSettings& settings, bool is2D) const;
	};
}
