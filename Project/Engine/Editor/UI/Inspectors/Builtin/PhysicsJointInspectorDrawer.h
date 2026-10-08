#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Physics/PhysicsJointComponent.h>

namespace Engine {

	//============================================================================
	//	FixedJointInspectorDrawer class
	//============================================================================
	class FixedJointInspectorDrawer :
		public SerializedComponentInspectorDrawer<FixedJointComponent> {
	public:
		FixedJointInspectorDrawer() : SerializedComponentInspectorDrawer("FixedJoint", "FixedJoint") {}
		~FixedJointInspectorDrawer() = default;
	private:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	//============================================================================
	//	HingeJointInspectorDrawer class
	//============================================================================
	class HingeJointInspectorDrawer :
		public SerializedComponentInspectorDrawer<HingeJointComponent> {
	public:
		HingeJointInspectorDrawer() : SerializedComponentInspectorDrawer("HingeJoint", "HingeJoint") {}
		~HingeJointInspectorDrawer() = default;
	private:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const HingeJointComponent& beforeComponent,
			HingeJointComponent& afterComponent) override;
	};
} // Engine
