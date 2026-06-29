#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>

namespace Engine {

	//============================================================================
	//	FillMeshRendererInspectorDrawer class
	//	FillMeshRendererComponentのインスペクター描画
	//============================================================================
	class FillMeshRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<FillMeshRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		FillMeshRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Fill Mesh Renderer", "FillMeshRenderer") {
		}
		~FillMeshRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
