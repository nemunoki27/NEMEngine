#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>

namespace Engine {

	//============================================================================
	//	UITextButtonInspectorDrawer class
	//	文字ボタンの操作を編集する
	//============================================================================
	class UITextButtonInspectorDrawer : public SerializedComponentInspectorDrawer<UITextButtonComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UITextButtonInspectorDrawer();
		~UITextButtonInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 設定項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};

} // Engine
