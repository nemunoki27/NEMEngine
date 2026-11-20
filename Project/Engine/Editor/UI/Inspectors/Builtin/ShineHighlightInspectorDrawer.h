#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ShineHighlightComponent.h>

namespace Engine {

	//============================================================================
	//	ShineHighlightInspectorDrawer class
	//	光の帯コンポーネントのインスペクター描画
	//============================================================================
	class ShineHighlightInspectorDrawer :
		public SerializedComponentInspectorDrawer<ShineHighlightComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ShineHighlightInspectorDrawer() :
			SerializedComponentInspectorDrawer("Shine Highlight", "ShineHighlight") {}
		~ShineHighlightInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
