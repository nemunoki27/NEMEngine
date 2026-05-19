#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>

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