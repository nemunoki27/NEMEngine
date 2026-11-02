#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>

namespace Engine {

	//============================================================================
	//	SimpleAnimationInspectorDrawer class
	//	単一値アニメーションの設定と再生操作を表示する
	//============================================================================
	class SimpleAnimationInspectorDrawer : public SerializedComponentInspectorDrawer<SimpleAnimationComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SimpleAnimationInspectorDrawer() : SerializedComponentInspectorDrawer("SimpleAnimation", "SimpleAnimation") {}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// 設定と再生状態を描画する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		// 再生状態を保持して編集設定だけを反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const SimpleAnimationComponent& component) override;
	};
}
