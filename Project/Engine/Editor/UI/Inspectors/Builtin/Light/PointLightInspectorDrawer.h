#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>

namespace Engine {

	//============================================================================
	//	PointLightInspectorDrawer class
	//	点コンポーネントのインスペクター描画
	//============================================================================
	class PointLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<PointLightComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PointLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("PointLight", "PointLight") {}
		~PointLightInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine