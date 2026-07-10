#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/FlipbookAnimationComponent.h>

namespace Engine {

	//============================================================================
	//	FlipbookAnimationInspectorDrawer class
	//	連番画像アニメーションコンポーネントのインスペクター描画
	//============================================================================
	class FlipbookAnimationInspectorDrawer :
		public SerializedComponentInspectorDrawer<FlipbookAnimationComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		FlipbookAnimationInspectorDrawer() :
			SerializedComponentInspectorDrawer("FlipbookAnimation", "FlipbookAnimation") {}
		~FlipbookAnimationInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
