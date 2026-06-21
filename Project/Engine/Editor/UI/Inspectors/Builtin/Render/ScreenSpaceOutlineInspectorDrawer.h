#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>

namespace Engine {

	//============================================================================
	//	ScreenSpaceOutlineInspectorDrawer class
	//	画面空間アウトラインコンポーネントのインスペクター描画
	//============================================================================
	class ScreenSpaceOutlineInspectorDrawer :
		public SerializedComponentInspectorDrawer<ScreenSpaceOutlineComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ScreenSpaceOutlineInspectorDrawer() :
			SerializedComponentInspectorDrawer("Screen Space Outline", "ScreenSpaceOutline") {}
		~ScreenSpaceOutlineInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine

