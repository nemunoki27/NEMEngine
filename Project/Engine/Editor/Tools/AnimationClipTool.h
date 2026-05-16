#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Interface/IEditorTool.h>
#include <Engine/Core/Animation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Property/AnimationPropertyRegistry.h>
#include <Engine/Editor/Animation/CurveEditorState.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	struct CurveChannelRef;

	//============================================================================
	//	AnimationClipTool class
	//	アニメーションの動きを作成するツール
	//============================================================================
	class AnimationClipTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AnimationClipTool();
		~AnimationClipTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// AnimationCurveウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.animation_clip",
			.name = "AnimationClip",
			.category = "AnimationClip",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		bool openWindow_ = false;

		// 編集対象のAnimationClipアセット。
		// Target Entityとは独立させ、Clip側にはEntity UUIDを書き込まない。
		AssetID clipAssetID_{};
		AssetID loadedClipAssetID_{};
		AnimationClipAsset clip_{};
		bool hasClip_ = false;
		bool clipDirty_ = false;
		std::string clipStatusText_;
		std::string clipErrorText_;

		// プレビュー適用先。HierarchyからD&Dで指定するが、Clip保存対象には含めない。
		UUID targetEntityUUID_{};

		// 複数Trackを同じCurveEditorで見るための状態。
		CurveEditorState curveState_{};
		// CurveEditorに表示しているTrack。複数PropertyのCurveが重ならないよう、選択中Trackだけを表示する。
		int selectedTrackIndex_ = -1;

		// Preview再生状態。previewActive_ はScrub中もtrueになり、Stop/Closeで必ず復元する。
		bool previewActive_ = false;
		bool previewPlaying_ = false;
		float previewTime_ = 0.0f;
		float previewSpeed_ = 1.0f;
		std::vector<AnimationPreviewBaseValue> previewBaseValues_;

		//--------- functions ----------------------------------------------------

		void DrawClipAssetUI(const EditorToolContext& context);
		void DrawTargetEntityUI(const EditorToolContext& context);
		void DrawPreviewControlUI(const EditorToolContext& context);
		void DrawPropertyListUI(const EditorToolContext& context);
		void DrawCurveEditorUI(const EditorToolContext& context);

		void LoadClipFromSelectedAsset(const EditorToolContext& context);
		void SaveClipToSelectedAsset(const EditorToolContext& context);
		void AddPropertyTrack(const AnimationPropertyDescriptor& desc, ECSWorld& world, const Entity& entity);

		Entity GetTargetEntity(const EditorToolContext& context) const;
		void NormalizeSelectedTrackIndex();
		void SyncCurveStateTime();
		void UpdatePreviewPlayback(const EditorToolContext& context);
		void ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive);
		void BeginPreview(const EditorToolContext& context);
		void EndPreviewAndRestore(const EditorToolContext& context);
		void CachePreviewBaseValues(ECSWorld& world, const Entity& entity);
		void RestorePreviewBaseValues(ECSWorld& world, const Entity& entity);
	};
} // Engine