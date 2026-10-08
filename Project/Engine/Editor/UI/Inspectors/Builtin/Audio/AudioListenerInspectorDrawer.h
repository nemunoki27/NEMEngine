#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Audio/AudioListenerComponent.h>

namespace Engine {

	// 受音Componentの設定を編集する
	class AudioListenerInspectorDrawer : public SerializedComponentInspectorDrawer<AudioListenerComponent> {
	public:
		AudioListenerInspectorDrawer();
	private:
		// 有効状態を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
}
