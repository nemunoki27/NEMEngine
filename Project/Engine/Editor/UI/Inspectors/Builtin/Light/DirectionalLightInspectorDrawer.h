#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>

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