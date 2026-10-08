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
		bool HasPendingEdits() const override;
		void RequestResolvePendingEdits() override;
		EditorToolCloseResult ConsumePendingEditCloseResult() override;
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
		std::optional<AssetID> pendingAsset_;
		std::string pendingCreate_;
		bool pendingClose_ = false;
		bool pendingConfirmation_ = false;
		bool resolvingClose_ = false;
		EditorToolCloseResult closeResult_ = EditorToolCloseResult::None;

		// 新規作成のファイル名入力
		std::string createNameBuffer_{};
		// 追加モジュールの検索
		TextSearchFilter addModuleSearchFilter_{};
		// ステータスメッセージ
		std::string statusMessage_{};

		//--------- functions ----------------------------------------------------

		// ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		void DrawPendingEdits(const EditorToolContext& context);
		void DrawHistory(const EditorToolContext& context);
		// アセットの選択と新規作成と保存を描画する
		void DrawAssetSection(const EditorToolContext& context);
		// グループの発生設定を描画する、変更があればtrue
		bool DrawGroupEmissionSection(const EditorToolContext& context);
		// グループ一覧を描画する、変更があればtrue
		bool DrawGroupList();

	};
} // Engine
