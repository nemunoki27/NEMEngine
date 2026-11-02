#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>

namespace Engine {

	class AnimationClipPlayerInspectorDrawer : public SerializedComponentInspectorDrawer<AnimationClipPlayerComponent> {
	public:
		AnimationClipPlayerInspectorDrawer() : SerializedComponentInspectorDrawer("AnimationClipPlayer", "AnimationClipPlayer") {}
	protected:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const AnimationClipPlayerComponent& component) override;
	};
}
