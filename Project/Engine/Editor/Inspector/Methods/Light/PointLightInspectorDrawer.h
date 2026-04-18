#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Light/PointLightComponent.h>

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