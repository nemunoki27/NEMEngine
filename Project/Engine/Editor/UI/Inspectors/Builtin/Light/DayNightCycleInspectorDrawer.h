#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/DayNightCycleComponent.h>

namespace Engine {

	//============================================================================
	//	DayNightCycleInspectorDrawer class
	//	時刻と太陽・月の参照を編集する
	//============================================================================
	class DayNightCycleInspectorDrawer : public SerializedComponentInspectorDrawer<DayNightCycleComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DayNightCycleInspectorDrawer() : SerializedComponentInspectorDrawer("DayNightCycle", "DayNightCycle") {}
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const DayNightCycleComponent& component) override;
	};
}
