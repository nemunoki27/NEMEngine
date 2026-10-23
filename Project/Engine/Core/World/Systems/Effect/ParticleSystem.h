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
	struct ParticleInstanceState;
	struct ParticleEmitterRuntime;

	//============================================================================
	//	ParticleSystem class
	//	ComponentのPhaseとModuleで粒子を発生、更新する
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

		ParticleInstanceUpdater instanceUpdater_;

		//--------- functions ----------------------------------------------------

		// コンポーネントの再生要求を実行状態へ反映する
		void ProcessCommands(const ParticleSystemComponent& component,
			ParticleSystemRuntimeData& runtime) const;
		// エミッターを先頭から再生する
		void StartEffect(ParticleSystemRuntimeData& runtime,
			bool oneShot) const;
		// エミッターの描画状態を破棄する
		void ClearEffect(ParticleSystemRuntimeData& runtime) const;
		// 再生完了時のEntity操作を適用する
		void ApplyStopAction(ECSWorld& world, const Entity& entity,
			ParticleSystemComponent& component, ParticleSystemRuntimeData& runtime,
			WorldMode mode) const;
	};
}
