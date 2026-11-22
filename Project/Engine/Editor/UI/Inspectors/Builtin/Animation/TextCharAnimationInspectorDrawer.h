#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Animation/TextCharAnimationComponent.h>

namespace Engine {

	class TextCharAnimationInspectorDrawer : public SerializedComponentInspectorDrawer<TextCharAnimationComponent> {
	public:
		TextCharAnimationInspectorDrawer() : SerializedComponentInspectorDrawer("Text CharAnimation", "TextCharAnimation") {}
	protected:
		void CopyEditFields(const TextCharAnimationComponent& source, TextCharAnimationComponent& draft) const override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const TextCharAnimationComponent& component) override;
	};
}
