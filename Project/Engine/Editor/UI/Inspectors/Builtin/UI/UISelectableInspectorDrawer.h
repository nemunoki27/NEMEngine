#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>

namespace Engine {

	//============================================================================
	//	UISelectableInspectorDrawer class
	//	UIの状態別表示を編集する
	//============================================================================
	class UISelectableInspectorDrawer : public SerializedComponentInspectorDrawer<UISelectableComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UISelectableInspectorDrawer();
		~UISelectableInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 設定項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		// 実行値へプレビューを反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const UISelectableComponent& previewComponent) override;
	};

} // Engine
