#include "AudioListenerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

Engine::AudioListenerInspectorDrawer::AudioListenerInspectorDrawer() :
	SerializedComponentInspectorDrawer("Audio Listener", "AudioListener") {
}

void Engine::AudioListenerInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, [[maybe_unused]] bool& anyItemActive) {

}
