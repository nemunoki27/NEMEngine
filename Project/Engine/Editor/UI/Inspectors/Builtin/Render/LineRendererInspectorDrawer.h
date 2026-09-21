#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	LineRendererInspectorDrawer class
	//	ラインレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class LineRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<LineRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		LineRendererInspectorDrawer() : SerializedComponentInspectorDrawer("Line Renderer", "LineRenderer") {}
		~LineRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// DynamicBufferから分離した編集中の点列
		std::vector<LinePoint> pointDraft_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		// ワールドの点列Bufferを編集用配列へ同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const LineRendererComponent& component) override;
		// 点列を含むドラフトを保存データへ変換する
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const LineRendererComponent& component, nlohmann::json& out) const override;
		// 固定長設定と点列をプレビューへ反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const LineRendererComponent& previewComponent) override;
	};
} // Engine
