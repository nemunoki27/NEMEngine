#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "AnimationControllerEditSession.h"
#include "AnimationControllerPreviewSession.h"

namespace Engine {

	//============================================================================
	//	AnimationControllerTool class
	//	状態、Parameter、遷移条件を編集する
	//============================================================================
	class AnimationControllerTool : public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		bool HasPendingEdits() const override;
		void RequestResolvePendingEdits() override;
		EditorToolCloseResult ConsumePendingEditCloseResult() override;
		void EndScenePreview() override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		void DrawPendingEdits(AssetDatabase& database);
		void DrawStates(AssetDatabase& database);
		void DrawParameters();
		void DrawTransitions();
		void DrawPreview(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.animation_controller",
			.name = "Animation Controller",
			.category = "アニメーション",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 3,
		};
		AnimationControllerEditSession session_;
		AnimationControllerPreviewSession preview_;
		uint64_t previewRevision_ = 0;
		std::optional<AssetID> pendingAsset_;
		bool openWindow_ = false;
		bool pendingClose_ = false;
		bool pendingReload_ = false;
		bool resolvingClose_ = false;
		EditorToolCloseResult closeResult_ = EditorToolCloseResult::None;
	};
}
