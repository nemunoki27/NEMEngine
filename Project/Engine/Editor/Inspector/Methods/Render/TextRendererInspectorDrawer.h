#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>

namespace Engine {

	//============================================================================
	//	TextRendererInspectorDrawer class
	//============================================================================
	class TextRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<TextRendererComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TextRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Text Renderer", "TextRenderer") {
		}
		~TextRendererInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// フォントアセット
		std::string fontBuffer_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine