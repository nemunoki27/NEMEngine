#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/ExponentialHeightFogComponent.h>

namespace Engine {

	class ExponentialHeightFogInspectorDrawer : public SerializedComponentInspectorDrawer<ExponentialHeightFogComponent> {
	public:
		ExponentialHeightFogInspectorDrawer() : SerializedComponentInspectorDrawer("ExponentialHeightFog", "ExponentialHeightFog") {}
	private:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
}
