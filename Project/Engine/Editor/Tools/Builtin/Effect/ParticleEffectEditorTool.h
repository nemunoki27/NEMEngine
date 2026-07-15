#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>
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

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// モジュールごとの編集用インスタンス、draft_.modulesと同じ並びで持つ
		struct ModuleCacheEntry {

			std::string id;
			std::unique_ptr<IParticleModule> module;
		};

		// グループごとのPhaseとModule編集状態
		struct GroupEditorState {

			int32_t selectedPhase = 0;
			std::vector<std::vector<ModuleCacheEntry>> moduleCache;
			std::vector<int32_t> selectedModules;
		};
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
		// 追加モジュールの検索
		TextSearchFilter addModuleSearchFilter_{};
		// 選択中のグループ
		UUID selectedGroupID_{};
		// ステータスメッセージ
		std::string statusMessage_{};

		// グループごとの編集状態
		std::unordered_map<UUID, GroupEditorState> groupEditorStates_;

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
			ParticleEffectGroup& group, GroupEditorState& editorState);
		// 選択フェーズのモジュール一覧を描画する、変更があればtrue
		bool DrawPhaseModules(const EditorToolContext& context, ParticleEffectGroup& group,
			GroupEditorState& editorState, ParticleEffectPhase& phase);
		// 選択フェーズのマテリアル設定を描画する、変更があればtrue
		bool DrawPhaseMaterialSection(const EditorToolContext& context,
			ParticleEffectGroup& group, ParticleEffectPhase& phase);
		// トレイルマテリアルのテクスチャ設定を描画する、変更があればtrue
		bool DrawTrailMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group);
		// 選択フェーズのペアレント設定を描画する、変更があればtrue
		bool DrawPhaseParentSection(const EditorToolContext& context,
			ParticleEffectGroup& group, ParticleEffectPhase& phase, int32_t selectedPhase);
		// 選択中のグループを取得する
		ParticleEffectGroup* GetSelectedGroup();
		// グループの編集状態を取得する
		GroupEditorState& GetGroupEditorState(UUID groupID);
		// モジュールの編集用インスタンスを取得する、idが変わっていれば作り直す
		IParticleModule* ResolveModuleCache(ModuleCacheEntry& cache, const ParticleEffectModuleEntry& entry);

		// 対象エフェクトを使っているエミッターを頭から再生する、oneShotはループを無視して1回だけ発生する
		void RestartEmitters(const EditorToolContext& context, bool oneShot);
		// 対象エフェクトを使っているエミッターを停止して粒子を消す
		void StopEmitters(const EditorToolContext& context);

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
