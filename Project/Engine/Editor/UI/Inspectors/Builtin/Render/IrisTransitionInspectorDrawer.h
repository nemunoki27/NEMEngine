#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/IrisTransitionComponent.h>

namespace Engine {

	//============================================================================
	//	IrisTransitionInspectorDrawer class
	//	アイリス遷移コンポーネントのインスペクター描画
	//============================================================================
	class IrisTransitionInspectorDrawer :
		public SerializedComponentInspectorDrawer<IrisTransitionComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IrisTransitionInspectorDrawer() :
			SerializedComponentInspectorDrawer("Iris Transition", "IrisTransition") {}
		~IrisTransitionInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const IrisTransitionComponent& previewComponent) override;
	};
} // Engine
