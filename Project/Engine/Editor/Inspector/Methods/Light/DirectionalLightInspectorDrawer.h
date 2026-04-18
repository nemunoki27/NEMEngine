#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Light/DirectionalLightComponent.h>

namespace Engine {

	//============================================================================
	//	DirectionalLightInspectorDrawer class
	//	平行光源コンポーネントのインスペクター描画
	//============================================================================
	class DirectionalLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<DirectionalLightComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DirectionalLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("DirectionalLight", "DirectionalLight") {}
		~DirectionalLightInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		
		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const DirectionalLightComponent& beforeComponent, DirectionalLightComponent& afterComponent) override;
	};
} // Engine