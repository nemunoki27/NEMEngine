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

	// front
	struct CurveChannelRef;

	enum class AnimationClipEditDimension :
		uint8_t {

		Auto,
		Mode2D,
		Mode3D,
	};

	enum class AnimationClipDetectedDimension :
		uint8_t {

		Unknown,
		Mode2D,
		Mode3D,
		Mixed,
	};

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

		//--------- structure ----------------------------------------------------

		// 選択中Channel/Trackへ波形キーをまとめて生成するための一時設定
		enum class GeneratorType :
			uint8_t {

			Sin,
			Cos,
			Easing,
		};
		enum class GeneratorApplyTo :
			uint8_t {

			SelectedChannel,
			SelectedTrack,
		};

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
		std::string clipErrorText_;

		// プレビュー適用先。HierarchyからD&Dで指定するが、Clip保存対象には含めない。
		UUID targetEntityUUID_{};

		// 複数Trackを同じCurveEditorで見るための状態。
		CurveEditorState curveState_{};
		// CurveEditorに表示しているTrack。複数PropertyのCurveが重ならないよう、選択中Trackだけを表示する。
		int selectedTrackIndex_ = -1;
		int editorViewTrackIndex_ = -1;

		// Preview再生状態。previewActive_ はScrub中もtrueになり、Stop/Closeで必ず復元する。
		bool previewActive_ = false;
		bool previewPlaying_ = false;
		float previewTime_ = 0.0f;
		float previewSpeed_ = 1.0f;
		std::vector<AnimationPreviewBaseValue> previewBaseValues_;

		// Transform系Propertyの2D/3D候補を絞るための表示フィルタ。
		AnimationClipEditDimension editDimension_ = AnimationClipEditDimension::Auto;

		GeneratorType generatorType_ = GeneratorType::Sin;
		GeneratorApplyTo generatorApplyTo_ = GeneratorApplyTo::SelectedChannel;
		float generatorStartTime_ = 0.0f;
		float generatorEndTime_ = 1.0f;
		float generatorStartValue_ = 0.0f;
		float generatorEndValue_ = 1.0f;
		float generatorAmplitude_ = 1.0f;
		float generatorFrequency_ = 1.0f;
		float generatorPhase_ = 0.0f;
		int generatorSampleCount_ = 16;
		int generatorEasingIndex_ = 0;
		bool generatorReplaceKeys_ = true;

		//--------- functions ----------------------------------------------------

		// アセット、編集設定UI
		void DrawToolbarUI(const EditorToolContext& context);
		void DrawClipAssetUI(const EditorToolContext& context);
		void DrawEditAssetUI(const EditorToolContext& context);

		void DrawPropertyTreeUI(const EditorToolContext& context);
		void DrawCurveEditorUI(const EditorToolContext& context);
		void DrawKeyInspectorUI(const EditorToolContext& context);
		void DrawGeneratorUI(const EditorToolContext& context);

		void LoadClipFromSelectedAsset(const EditorToolContext& context);
		void SaveClipToSelectedAsset(const EditorToolContext& context);
		// Propertyを追加した時点ではキーを作らず、現在値をdefaultValueとして保持する。
		void AddPropertyTrack(const AnimationPropertyDescriptor& desc, ECSWorld& world, const Entity& entity);
		void RevertClipFromSelectedAsset(const EditorToolContext& context);

		Entity GetTargetEntity(const EditorToolContext& context) const;
		AnimationClipDetectedDimension DetectTargetDimension(const EditorToolContext& context) const;
		AnimationClipEditDimension GetEffectiveEditDimension(const EditorToolContext& context) const;
		void NormalizeSelectedTrackIndex();
		void StoreSelectedTrackEditorView();
		void LoadSelectedTrackEditorView();
		void SyncCurveStateTime();
		void UpdatePreviewPlayback(const EditorToolContext& context);
		// 現在時刻のClip評価値をTarget Entityへ直接反映する。
		void ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive);
		void BeginPreview(const EditorToolContext& context);
		void EndPreviewAndRestore(const EditorToolContext& context);
		// Preview終了時に戻せるよう、編集前の値だけを保持する。
		void CachePreviewBaseValues(ECSWorld& world, const Entity& entity);
		void RestorePreviewBaseValues(ECSWorld& world, const Entity& entity);
		void AddKeyToChannel(AnimationCurveTrack& track, uint32_t channelIndex, float time);
		void UpdateAutoDurationAndPreview(const EditorToolContext& context);
	};
} // Engine
