#pragma once

//============================================================================
//	include
//============================================================================
#include "Modules/IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Foundation/Utility/Command/CommandHistory.h>

#include <unordered_map>
#include <optional>

namespace Engine {

	struct ParticleModuleEditCacheEntry {

		UUID instanceID{};
		std::string id;
		ParticleModuleRegistry::TypeID typeID = ParticleModuleRegistry::kInvalidTypeID;
		std::unique_ptr<IParticleModule> module;
		std::unique_ptr<IParticleModuleDrawer> drawer;
	};

	struct ParticleGroupEditState {

		int32_t selectedPhase = 0;
		std::vector<std::vector<ParticleModuleEditCacheEntry>> moduleCache;
		std::vector<int32_t> selectedModules;
	};

	//============================================================================
	//	ParticleEffectEditSession class
	//	Effectの編集値とモジュール編集状態を所有する
	//============================================================================
	class ParticleEffectEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 選択中のグループを取得する
		ParticleEffectGroup* GetSelectedGroup();
		// グループの編集状態を取得する
		ParticleGroupEditState& GetGroupEditorState(UUID groupID);
		// モジュールの編集用インスタンスを取得する、idが変わっていれば作り直す
		IParticleModule* ResolveModuleCache(ParticleModuleEditCacheEntry& cache, const ParticleEffectModuleEntry& entry);
		// エフェクトをファイルから読み込む
		bool LoadEffect(const EditorToolContext& context, AssetID effectID, std::string& statusMessage);
		// エフェクトをファイルへ保存する
		bool SaveEffect(const EditorToolContext& context, std::string& statusMessage);
		// 新規エフェクトを作成する
		bool CreateEffect(const EditorToolContext& context, std::string& statusMessage, const std::string& createName);
		// 編集内容をランタイムへ即反映する
		void ApplyToRuntime();
		void UpdateEditing(bool changed, bool itemActive);
		void FinishEditing();
		bool Undo();
		bool Redo();
		void Discard();

		// 削除したグループの編集状態を破棄する
		void RemoveGroupState(UUID groupID);

		//--------- accessor -----------------------------------------------------

		ParticleEffectAsset& GetDraft() { return draft_; }
		const ParticleEffectAsset& GetDraft() const { return draft_; }
		AssetID GetEditingID() const { return editingID_; }
		bool IsLoaded() const { return loaded_; }
		bool IsDirty() const { return dirty_; }
		bool CanUndo() const { return history_.CanUndo() || pendingBefore_.has_value(); }
		bool CanRedo() const { return history_.CanRedo(); }
		UUID& GetSelectedGroupID() { return selectedGroupID_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID editingID_{};
		ParticleEffectAsset draft_{};
		ParticleEffectAsset savedDraft_{};
		ParticleEffectAsset committedDraft_{};
		std::optional<ParticleEffectAsset> pendingBefore_;
		CommandHistory<ParticleEffectAsset> history_;
		bool loaded_ = false;
		bool dirty_ = false;
		UUID selectedGroupID_{};
		std::unordered_map<UUID, ParticleGroupEditState> groupEditorStates_;
	};
}
