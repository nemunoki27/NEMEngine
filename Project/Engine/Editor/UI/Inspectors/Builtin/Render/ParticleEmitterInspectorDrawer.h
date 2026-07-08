#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>

namespace Engine {

	//============================================================================
	//	ParticleEmitterInspectorDrawer class
	//	ParticleEmitterComponentのインスペクター描画
	//============================================================================
	class ParticleEmitterInspectorDrawer :
		public SerializedComponentInspectorDrawer<ParticleEmitterComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ParticleEmitterInspectorDrawer() :
			SerializedComponentInspectorDrawer("Particle Emitter", "ParticleEmitter") {
		}
		~ParticleEmitterInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
