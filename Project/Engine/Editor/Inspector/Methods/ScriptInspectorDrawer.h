#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/ScriptComponent.h>

namespace Engine {

	//============================================================================
	//	ScriptInspectorDrawer class
	//	スクリプトコンポーネントのインスペクター描画
	//============================================================================
	class ScriptInspectorDrawer :
		public SerializedComponentInspectorDrawer<ScriptComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptInspectorDrawer() :
			SerializedComponentInspectorDrawer("Script", "Script") {
		}
		~ScriptInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine