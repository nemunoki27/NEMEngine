#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>

namespace Engine {

	//============================================================================
	//	DecalProjectorInspectorDrawer class
	//	投影範囲と個別Material値を編集する
	//============================================================================
	class DecalProjectorInspectorDrawer : public SerializedComponentInspectorDrawer<DecalProjectorComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DecalProjectorInspectorDrawer() : SerializedComponentInspectorDrawer("DecalProjector", "DecalProjector") {}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 投影とMaterialの変更を編集Sessionへ渡す
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
}
