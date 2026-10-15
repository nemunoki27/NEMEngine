#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ReflectionProbeComponent.h>

namespace Engine {

	//============================================================================
	//	ReflectionProbeInspectorDrawer class
	//	反射Captureの形状と範囲を編集する
	//============================================================================
	class ReflectionProbeInspectorDrawer : public SerializedComponentInspectorDrawer<ReflectionProbeComponent> {
	public:
		ReflectionProbeInspectorDrawer() : SerializedComponentInspectorDrawer("Reflection Probe", "ReflectionProbe") {}
		~ReflectionProbeInspectorDrawer() override = default;
	protected:
		// 編集値を描画する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
}
