#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleInstanceUpdater.h"
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	// front
	struct ParticleSystemComponent;
	struct ParticleSystemRuntimeData;
	struct ParticleEffectInstanceRuntime;
	struct ParticleGroupRuntimeState;

	//============================================================================
	//	ParticleSystem class
	//	エフェクトアセットのモジュールでパーティクルを発生、更新する
	//============================================================================
	class ParticleSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSystem() = default;
		~ParticleSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "ParticleSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const float kReloadCheckInterval = 0.5f;
		float reloadCheckTimer_ = 0.0f;
		ParticleInstanceUpdater instanceUpdater_;

		//--------- functions ----------------------------------------------------

		// コンポーネントの再生要求を実行状態へ反映する
		void ProcessCommands(const ParticleSystemComponent& component,
			ParticleSystemRuntimeData& runtime) const;
		// ParticleEffectを先頭から再生する
		void StartEffect(ParticleSystemRuntimeData& runtime,
			AssetID effectID, bool oneShot) const;
		// ParticleEffectの描画状態を破棄する
		void ClearEffect(ParticleSystemRuntimeData& runtime) const;
		// 再生完了時のEntity操作を適用する
		void ApplyStopAction(ECSWorld& world, const Entity& entity,
			ParticleSystemComponent& component, ParticleSystemRuntimeData& runtime,
			WorldMode mode) const;
	};
}
