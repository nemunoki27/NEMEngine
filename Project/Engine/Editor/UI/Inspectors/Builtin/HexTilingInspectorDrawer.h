#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/HexTilingComponent.h>

namespace Engine {

	//============================================================================
	//	HexTilingInspectorDrawer class
	//	六角形タイリングコンポーネントのインスペクター描画
	//============================================================================
	class HexTilingInspectorDrawer :
		public SerializedComponentInspectorDrawer<HexTilingComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		HexTilingInspectorDrawer() :
			SerializedComponentInspectorDrawer("HexTiling", "HexTiling") {}
		~HexTilingInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
