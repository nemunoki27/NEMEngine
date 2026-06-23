#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>

namespace Engine {

	//============================================================================
	//	LineRendererInspectorDrawer class
	//	ラインレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class LineRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<LineRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		LineRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Line Renderer", "LineRenderer") {
		}
		~LineRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
