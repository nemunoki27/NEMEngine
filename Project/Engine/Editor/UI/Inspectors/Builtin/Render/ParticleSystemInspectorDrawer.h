#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace Engine {

	//============================================================================
	//	ParticleSystemInspectorDrawer class
	//	ParticleSystemComponentのインスペクター描画
	//============================================================================
	class ParticleSystemInspectorDrawer :
		public SerializedComponentInspectorDrawer<ParticleSystemComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSystemInspectorDrawer() :
			SerializedComponentInspectorDrawer("Particle System", "ParticleSystem") {
		}
		~ParticleSystemInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
