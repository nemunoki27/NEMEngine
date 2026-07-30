#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>

// c++
#include <vector>

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

		//--------- variables ----------------------------------------------------

		// DynamicBufferから分離した編集中の面頂点
		std::vector<Vector3> positionDraft_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		// ワールドの編集点列をドラフトへ同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const FillMeshRendererComponent& component) override;
		// 編集点列を含む保存データへ変換する
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const FillMeshRendererComponent& component, nlohmann::json& out) const override;
		// 固定長設定と編集点列をプレビューへ反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const FillMeshRendererComponent& previewComponent) override;
	};
} // Engine
