#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectDefinition.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::ParticleParenting {

	// 通常再生とエフェクト作成ツールで使用する親設定を切り替える
	const ParticlePhaseParentSettings& ResolveParticleParentSettings(const ParticlePhaseDefinition& phase,
		const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings);
	// 現在フェーズの親設定を粒子へ反映する
	void UpdateParticleParent(Particle& particle, const ParticlePhaseParentSettings& settings,
		const ParticleParentPose& parent, bool preserveWorldRotationScale = true);
	// 全粒子の親行列と描画用ワールド姿勢を更新する
	void UpdateParticleParents(std::vector<Particle>& particles, const ParticleGroupDefinition& group,
		const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
		const std::vector<ParticleParentPose>& parents);
	// 各フェーズの親姿勢をエミッター単位で解決する
	void ResolveParticleParents(ECSWorld& world, const Matrix4x4& emitterWorld, const ParticleGroupDefinition& group,
		const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
		std::vector<ParticleParentPose>& outParents);
	// 粒子の描画用ワールド姿勢を更新する
	void RefreshParticleWorldTransform(Particle& particle, const ParticleParentPose* parent);
}
