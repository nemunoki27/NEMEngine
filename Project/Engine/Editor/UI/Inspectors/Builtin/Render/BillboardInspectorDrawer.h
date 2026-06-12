#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>

namespace Engine {

	//============================================================================
	//	BillboardInspectorDrawer class
	//	ビルボードコンポーネントのインスペクター描画
	//============================================================================
	class BillboardInspectorDrawer :
		public SerializedComponentInspectorDrawer<BillboardComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		BillboardInspectorDrawer() :
			SerializedComponentInspectorDrawer("Billboard", "Billboard") {}
		~BillboardInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
} // Engine

