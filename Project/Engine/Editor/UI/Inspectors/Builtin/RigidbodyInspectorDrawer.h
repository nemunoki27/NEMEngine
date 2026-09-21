#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>

namespace Engine {

	//============================================================================
	//	RigidbodyInspectorDrawer class
	//	Rigidbodyコンポーネントのインスペクター描画
	//============================================================================
	class RigidbodyInspectorDrawer :
		public SerializedComponentInspectorDrawer<RigidbodyComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RigidbodyInspectorDrawer() : SerializedComponentInspectorDrawer("Rigidbody", "Rigidbody") {}
		~RigidbodyInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
