#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectDefinitionCache.h"
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

		// ParticleEffect1つ分を更新する
		bool UpdateEffectInstance(ECSWorld& world,
			ParticleEffectInstanceRuntime& instance, const Matrix4x4& emitterWorld,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			SystemContext& context, float deltaTime, bool updateSimulation,
			bool emissionEnabled, bool drawEmitterShape, bool checkReload);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleEffectDefinitionCache definitions_;
		ParticleTrailUpdater trails_;
		std::vector<ParticleParentPose> parentRuntimes_;

		//--------- functions ----------------------------------------------------

		// ParticleEffect実行状態をアセットのグループ順へ同期する
		bool SynchronizeRuntimeGroups(
			ParticleEffectInstanceRuntime& instance, const ParticleEffectDefinition& effect) const;
		// ParticleEffectの再生状態を先頭へ戻す
		void RestartEffectInstance(ParticleEffectInstanceRuntime& instance, const ParticleEffectAsset& asset) const;
		// 同時発生を行うフレームか判定する
		bool UpdateGroupEmission(ParticleEffectInstanceRuntime& instance,
			const ParticleEffectAsset& asset, float deltaTime, bool emissionEnabled) const;
		// グループ1つ分の粒子とトレイルを更新する
		void UpdateGroup(ECSWorld& world, const Matrix4x4& emitterWorld,
			ParticleGroupRuntimeState& state, const ParticleEffectAsset& asset,
			const ParticleEffectGroup& group, const ParticleGroupDefinition& runtime,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			float deltaTime, bool updateSimulation, bool simultaneousEmit, bool emissionEnabled,
			bool oneShot, bool drawEmitterShape);
		// エミッター形状から発生位置と方向と初期状態を決める、firstSpawnIndexは発生順の連番の開始値
		void InitEmitterParticles(std::span<Particle> newborn, const ParticleEmitterSettings& settings,
			const ParticleValue<float>& lifetime, bool is2D, uint32_t firstSpawnIndex) const;
		// エミッター形状をデバッグ線で描画する
		void DrawEmitterShape(const Matrix4x4& emitterWorld, const ParticleEmitterSettings& settings, bool is2D) const;
	};
}
