#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/AudioSourceComponent.h>

namespace Engine {

	//============================================================================
	//	AudioSourceInspectorDrawer class
	//	AudioSourceコンポーネントのインスペクター描画
	//============================================================================
	class AudioSourceInspectorDrawer :
		public SerializedComponentInspectorDrawer<AudioSourceComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AudioSourceInspectorDrawer() :
			SerializedComponentInspectorDrawer("Audio Source", "AudioSource") {
		}
		~AudioSourceInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
