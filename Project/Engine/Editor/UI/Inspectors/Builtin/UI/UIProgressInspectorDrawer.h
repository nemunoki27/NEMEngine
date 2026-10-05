#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>

namespace Engine {

	//============================================================================
	//	UIProgressInspectorDrawer class
	//	進行量と遅延表示を編集する
	//============================================================================
	class UIProgressInspectorDrawer : public SerializedComponentInspectorDrawer<UIProgressComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIProgressInspectorDrawer();
		~UIProgressInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 設定項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		// 実行値へプレビューを反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const UIProgressComponent& previewComponent) override;
	};

} // Engine
