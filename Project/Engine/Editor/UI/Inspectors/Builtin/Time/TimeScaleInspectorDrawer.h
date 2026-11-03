#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

namespace Engine {

	class TimeScaleInspectorDrawer : public SerializedComponentInspectorDrawer<TimeScaleComponent> {
	public:
		TimeScaleInspectorDrawer() : SerializedComponentInspectorDrawer("TimeScale", "TimeScale") {}
	private:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
}
