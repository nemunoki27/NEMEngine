#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleEffectEditorTool class
	//	パーティクルエフェクトアセットの作成、編集を行う、編集は保存なしで即シーンへ反映する
	//============================================================================
	class ParticleEffectEditorTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEffectEditorTool() = default;
		~ParticleEffectEditorTool() = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.particle_effect_editor",
			.name = "エフェクト編集",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 2,
		};

		// ウィンドウの表示状態
		bool openWindow_ = false;

		// 編集中のエフェクト
		AssetID editingID_{};
		ParticleEffectAsset draft_{};
		bool loaded_ = false;

		// 新規作成のファイル名入力
		std::string createNameBuffer_{};
		// 追加するモジュールの選択位置
		int32_t addModuleIndex_ = 0;
		// ステータスメッセージ
		std::string statusMessage_{};

		// モジュールごとのカーブ編集状態
		std::unordered_map<int32_t, CurveEditorState> curveStates_;

		//--------- functions ----------------------------------------------------

		// ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// アセットの選択と新規作成と保存を描画する
		void DrawAssetSection(const EditorToolContext& context);
		// 再生と描画の基本設定を描画する、変更があればtrue
		bool DrawBasicSection(const EditorToolContext& context);
		// モジュール一覧を描画する、変更があればtrue
		bool DrawModuleSection();
		// モジュールのパラメータ編集UIを描画する、変更があればtrue
		bool DrawModuleParams(const std::string& id, nlohmann::json& params, int32_t moduleIndex);

		// エフェクトをファイルから読み込む
		void LoadEffect(const EditorToolContext& context, AssetID effectID);
		// エフェクトをファイルへ保存する
		void SaveEffect(const EditorToolContext& context);
		// 新規エフェクトを作成する
		void CreateEffect(const EditorToolContext& context);
		// 編集内容をランタイムへ即反映する
		void ApplyToRuntime();
	};
} // Engine
