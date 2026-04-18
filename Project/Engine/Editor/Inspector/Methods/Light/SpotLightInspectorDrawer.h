#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Light/SpotLightComponent.h>

namespace Engine {

	//============================================================================
	//	SpotLightInspectorDrawer class
	//	スポットコンポーネントのインスペクター描画
	//============================================================================
	class SpotLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<SpotLightComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SpotLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("SpotLight", "SpotLight") {}
		~SpotLightInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const SpotLightComponent& beforeComponent, SpotLightComponent& afterComponent) override;
	};
} // Engine