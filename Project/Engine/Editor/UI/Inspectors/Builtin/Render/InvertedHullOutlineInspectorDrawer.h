#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>

namespace Engine {

	//============================================================================
	//	InvertedHullOutlineInspectorDrawer class
	//	背面法アウトラインコンポーネントのインスペクター描画
	//============================================================================
	class InvertedHullOutlineInspectorDrawer :
		public SerializedComponentInspectorDrawer<InvertedHullOutlineComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		InvertedHullOutlineInspectorDrawer() :
			SerializedComponentInspectorDrawer("Inverted Hull Outline", "InvertedHullOutline") {}
		~InvertedHullOutlineInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
