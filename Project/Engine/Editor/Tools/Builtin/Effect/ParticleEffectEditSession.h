#pragma once

//============================================================================
//	include
//============================================================================
#include "Modules/IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

#include <unordered_map>

namespace Engine {

	struct ParticleModuleEditCacheEntry {

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
		void LoadEffect(const EditorToolContext& context, AssetID effectID, std::string& statusMessage);
		// エフェクトをファイルへ保存する
		void SaveEffect(const EditorToolContext& context, std::string& statusMessage);
		// 新規エフェクトを作成する
		void CreateEffect(const EditorToolContext& context, std::string& statusMessage, const std::string& createName);
		// 編集内容をランタイムへ即反映する
		void ApplyToRuntime();

		// 削除したグループの編集状態を破棄する
		void RemoveGroupState(UUID groupID);

		//--------- accessor -----------------------------------------------------

		ParticleEffectAsset& GetDraft() { return draft_; }
		const ParticleEffectAsset& GetDraft() const { return draft_; }
		AssetID GetEditingID() const { return editingID_; }
		bool IsLoaded() const { return loaded_; }
		UUID& GetSelectedGroupID() { return selectedGroupID_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID editingID_{};
		ParticleEffectAsset draft_{};
		bool loaded_ = false;
		UUID selectedGroupID_{};
		std::unordered_map<UUID, ParticleGroupEditState> groupEditorStates_;
	};
}
