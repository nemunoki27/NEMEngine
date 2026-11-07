#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>

namespace Engine {

	class SkyAtmosphereInspectorDrawer : public SerializedComponentInspectorDrawer<SkyAtmosphereComponent> {
	public:
		SkyAtmosphereInspectorDrawer() : SerializedComponentInspectorDrawer("SkyAtmosphere", "SkyAtmosphere") {}
	private:
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
}
