#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/EffectEmitterComponent.h>

namespace Engine {

	//============================================================================
	//	EffectEmitterInspectorDrawer class
	//	EffectEmitterComponentのインスペクター描画
	//============================================================================
	class EffectEmitterInspectorDrawer :
		public SerializedComponentInspectorDrawer<EffectEmitterComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EffectEmitterInspectorDrawer() :
			SerializedComponentInspectorDrawer("Effect Emitter", "EffectEmitter") {
		}
		~EffectEmitterInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const EffectEmitterComponent& previewComponent) override;
	};
} // Engine
