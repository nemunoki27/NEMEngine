#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectDefinition.h"
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	ParticleTrailUpdater class
	//	トレイルの軌跡更新と生存IDの作業領域を管理する
	//============================================================================
	class ParticleTrailUpdater {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// トレイルの軌跡点をワールド空間で記録し、死亡した粒子から切り離す
		void RecordTrails(ParticleGroupRuntimeState& state, const ParticleTrailSettings& trail, float deltaTime);
		// 粒子消滅後に退避したトレイル所有者を更新する
		void UpdateDetachedTrailOwners(ParticleGroupRuntimeState& state, const ParticleGroupDefinition& group,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			const std::vector<ParticleParentPose>& parents, const ParticleTrailSettings& trail, float deltaTime) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_set<uint32_t> aliveTrailIDs_;
	};
}
