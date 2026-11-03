#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Time/TimerComponent.h>

namespace Engine {

	//============================================================================
	//	TimerInspectorDrawer class
	//	タイマーの時間設定と再生状態を編集する
	//============================================================================
	class TimerInspectorDrawer : public SerializedComponentInspectorDrawer<TimerComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TimerInspectorDrawer() : SerializedComponentInspectorDrawer("Timer", "Timer") {}
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const TimerComponent& component) override;
	};
}
