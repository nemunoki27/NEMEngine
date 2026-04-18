#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h>

namespace Engine {

	//============================================================================
	//	SkinnedAnimationInspectorDrawer class
	//	骨アニメーションコンポーネントのインスペクター描画
	//============================================================================
	class SkinnedAnimationInspectorDrawer :
		public SerializedComponentInspectorDrawer<SkinnedAnimationComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SkinnedAnimationInspectorDrawer() :
			SerializedComponentInspectorDrawer("SkinnedAnimation", "SkinnedAnimation") {}
		~SkinnedAnimationInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine