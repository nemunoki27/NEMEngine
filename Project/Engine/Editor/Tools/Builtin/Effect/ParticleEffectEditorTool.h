#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "ParticleEffectEditSession.h"
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

// c++
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
		// ProjectPanelから指定エフェクトを開く
		void OpenAsset(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Effectの編集セッション
		ParticleEffectEditSession session_;

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
		AssetID pendingAsset_{};

		// 新規作成のファイル名入力
		std::string createNameBuffer_{};
		// 追加モジュールの検索
		TextSearchFilter addModuleSearchFilter_{};
		// ステータスメッセージ
		std::string statusMessage_{};

		//--------- functions ----------------------------------------------------

		// ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// アセットの選択と新規作成と保存を描画する
		void DrawAssetSection(const EditorToolContext& context);
		// グループの発生設定を描画する、変更があればtrue
		bool DrawGroupEmissionSection(const EditorToolContext& context);
		// グループ一覧を描画する、変更があればtrue
		bool DrawGroupList();
		// 再生と描画の基本設定を描画する、変更があればtrue
		bool DrawBasicSection(const EditorToolContext& context, ParticleEffectGroup& group);
		// フェーズ一覧と選択フェーズの編集を描画する、変更があればtrue
		bool DrawPhaseSection(const EditorToolContext& context,
			ParticleEffectGroup& group, ParticleGroupEditState& editorState);
		// 選択フェーズのモジュール一覧を描画する、変更があればtrue
		bool DrawPhaseModules(const EditorToolContext& context, ParticleEffectGroup& group,
			ParticleGroupEditState& editorState, ParticleEffectPhase& phase);
		// 選択フェーズのマテリアル設定を描画する、変更があればtrue
		bool DrawPhaseMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group, ParticleEffectPhase& phase);
		// トレイルマテリアルのテクスチャ設定を描画する、変更があればtrue
		bool DrawTrailMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group);
		// 選択フェーズのペアレント設定を描画する、変更があればtrue
		bool DrawPhaseParentSection(const EditorToolContext& context,
			ParticleEffectGroup& group, ParticleEffectPhase& phase, int32_t selectedPhase);

	};
} // Engine
