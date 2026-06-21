#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>

namespace Engine {

	//============================================================================
	//	Rigidbody2DInspectorDrawer class
	//	Rigidbody2Dコンポーネントのインスペクター描画
	//============================================================================
	class Rigidbody2DInspectorDrawer :
		public SerializedComponentInspectorDrawer<Rigidbody2DComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		Rigidbody2DInspectorDrawer() :
			SerializedComponentInspectorDrawer("Rigidbody 2D", "Rigidbody2D") {
		}
		~Rigidbody2DInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
