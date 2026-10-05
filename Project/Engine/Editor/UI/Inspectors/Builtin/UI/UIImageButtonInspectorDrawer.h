#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>

namespace Engine {

	//============================================================================
	//	UIImageButtonInspectorDrawer class
	//	画像ボタンの操作を編集する
	//============================================================================
	class UIImageButtonInspectorDrawer : public SerializedComponentInspectorDrawer<UIImageButtonComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIImageButtonInspectorDrawer();
		~UIImageButtonInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 設定項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};

} // Engine
