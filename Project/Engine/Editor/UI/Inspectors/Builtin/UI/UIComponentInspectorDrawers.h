#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIButtonComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>

namespace Engine {

	//============================================================================
	//	UIComponentInspectorDrawers classes
	//============================================================================
	// キャンバス
	class CanvasInspectorDrawer :
		public SerializedComponentInspectorDrawer<CanvasComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CanvasInspectorDrawer() : SerializedComponentInspectorDrawer("Canvas", "Canvas") {}
		~CanvasInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	class UISelectableInspectorDrawer :
		public SerializedComponentInspectorDrawer<UISelectableComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UISelectableInspectorDrawer() : SerializedComponentInspectorDrawer("UI Selectable", "UISelectable") {}
		~UISelectableInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	class UIButtonInspectorDrawer :
		public SerializedComponentInspectorDrawer<UIButtonComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIButtonInspectorDrawer() : SerializedComponentInspectorDrawer("UI Button", "UIButton") {}
		~UIButtonInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	class UIProgressInspectorDrawer :
		public SerializedComponentInspectorDrawer<UIProgressComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIProgressInspectorDrawer() : SerializedComponentInspectorDrawer("UI Progress", "UIProgress") {}
		~UIProgressInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
