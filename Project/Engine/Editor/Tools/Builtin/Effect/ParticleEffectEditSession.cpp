#include "ParticleEffectEditSession.h"
#include "ParticleEditorDescriptorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

#include <algorithm>
#include <filesystem>

using namespace Engine;

ParticleEffectGroup* ParticleEffectEditSession::GetSelectedGroup() {

	auto it = std::find_if(draft_.groups.begin(), draft_.groups.end(), [&](const ParticleEffectGroup& group) {
		return group.id == selectedGroupID_;
		});
	if (it != draft_.groups.end()) { return &*it; }
	if (draft_.groups.empty()) { return nullptr; }
	selectedGroupID_ = draft_.groups.front().id;
	return &draft_.groups.front();
}

ParticleGroupEditState& ParticleEffectEditSession::GetGroupEditorState(UUID groupID) {

	return groupEditorStates_[groupID];
}

Engine::IParticleModule* ParticleEffectEditSession::ResolveModuleCache(ParticleModuleEditCacheEntry& cache, const ParticleEffectModuleEntry& entry) {

	// idが変わっていたら作り直し、現在のパラメータを読み込ませる
	if (!cache.module || cache.id != entry.id) {

		cache.id = entry.id;
		ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
		cache.typeID = registry.FindTypeID(entry.id);
		cache.module = registry.Create(cache.typeID);
		cache.drawer = ParticleEditorDescriptorRegistry::GetInstance().CreateModuleDrawer(cache.typeID);
		if (cache.module) {
			cache.module->FromJson(entry.params);
		}
	}
	return cache.module.get();
}

void ParticleEffectEditSession::LoadEffect(const EditorToolContext& context, AssetID effectID, std::string& statusMessage) {

	loaded_ = false;
	editingID_ = effectID;
	groupEditorStates_.clear();
	selectedGroupID_ = {};
	if (!effectID || !context.toolContext.assetDatabase) {
		return;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(effectID);
	if (path.empty()) {

		statusMessage = "エフェクトファイルが見つかりません";
		return;
	}
	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!FromJson(data, draft_)) {

		statusMessage = "エフェクトファイルの読み込みに失敗しました";
		return;
	}
	selectedGroupID_ = draft_.groups.front().id;
	loaded_ = true;
	statusMessage.clear();
}

void ParticleEffectEditSession::SaveEffect(const EditorToolContext& context, std::string& statusMessage) {

	if (!loaded_ || !editingID_ || !context.toolContext.assetDatabase) {
		return;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(editingID_);
	if (path.empty()) {

		statusMessage = "保存先のパスを解決できません";
		return;
	}
	JsonAdapter::Save(path.string(), ToJson(draft_));
	statusMessage = "保存しました: " + path.filename().string();
}

void ParticleEffectEditSession::CreateEffect(const EditorToolContext& context, std::string& statusMessage, const std::string& createName) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || createName.empty()) {
		return;
	}

	// 既定のフェーズ構成で新規エフェクトを作る
	ParticleEffectAsset asset{};
	asset.name = createName;
	ParticleEffectGroup group{};
	group.name = "Group 1";
	ParticleEffectPhase phase{};
	phase.name = "Phase 1";
	phase.modules = {
		{ "SizeOverLifetime", nlohmann::json::object() },
		{ "ColorOverLifetime", nlohmann::json::object() },
	};
	group.phases.emplace_back(std::move(phase));
	asset.groups.emplace_back(std::move(group));

	const std::string logical = "GameAssets/Effects/" + createName + ".effect.json";
	const std::filesystem::path path = assetDatabase->ResolveAssetPath(logical);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	JsonAdapter::Save(path.string(), ToJson(asset));

	const AssetID assetID = assetDatabase->ImportOrGet(logical, AssetType::ParticleEffect);
	if (!assetID) {

		statusMessage = "エフェクトの作成に失敗しました";
		return;
	}
	statusMessage = "作成しました: " + logical;
	LoadEffect(context, assetID, statusMessage);
}

void ParticleEffectEditSession::ApplyToRuntime() {

	if (!loaded_ || !editingID_) {
		return;
	}
	ParticleEffectEditBridge::GetInstance().Push(editingID_, draft_);
}

void ParticleEffectEditSession::RemoveGroupState(UUID groupID) {

	groupEditorStates_.erase(groupID);
}
