#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>

namespace Engine {

	//============================================================================
	//	AnimationPlayerInspectorDrawer class
	//	プロパティアニメ再生コンポーネントのインスペクター描画
	//============================================================================
	class AnimationPlayerInspectorDrawer :
		public SerializedComponentInspectorDrawer<AnimationPlayerComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AnimationPlayerInspectorDrawer() :
			SerializedComponentInspectorDrawer("AnimationPlayer", "AnimationPlayer") {}
		~AnimationPlayerInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
