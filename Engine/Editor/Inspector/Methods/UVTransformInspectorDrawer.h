#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/UVTransformComponent.h>

namespace Engine {

	//============================================================================
	//	UVTransformInspectorDrawer class
	//	UVトランスフォームコンポーネントのインスペクター描画
	//============================================================================
	class UVTransformInspectorDrawer :
		public SerializedComponentInspectorDrawer<UVTransformComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		UVTransformInspectorDrawer() :
			SerializedComponentInspectorDrawer("UVTransform", "UVTransform") {}
		~UVTransformInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine