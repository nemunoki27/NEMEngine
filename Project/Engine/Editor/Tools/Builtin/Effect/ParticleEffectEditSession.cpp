#include "ParticleEffectEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

#include <algorithm>
#include <filesystem>
#include <exception>

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

bool ParticleEffectEditSession::LoadEffect(const EditorToolContext& context, AssetID effectID, std::string& statusMessage) {

	if (!effectID || !context.toolContext.assetDatabase) {
		return false;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(effectID);
	if (path.empty()) {

		statusMessage = "エフェクトファイルが見つかりません";
		return false;
	}
	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	ParticleEffectAsset replacement{};
	try {
		if (!FromJson(data, replacement)) {
			statusMessage = "エフェクトファイルの読み込みに失敗しました";
			return false;
		}
	} catch (const std::exception& error) {

		statusMessage = "エフェクトファイルの読み込みに失敗しました: " + std::string(error.what());
		return false;
	}
	// 読込成功後に以前の未保存上書きと履歴を解除する
	ParticleEffectAsset saved = replacement;
	ParticleEffectAsset committed = replacement;
	ParticleEffectEditBridge::GetInstance().Remove(editingID_);
	draft_ = std::move(replacement);
	savedDraft_ = std::move(saved);
	committedDraft_ = std::move(committed);
	editingID_ = effectID;
	groupEditorStates_.clear();
	pendingBefore_.reset();
	history_.Clear();
	dirty_ = false;
	selectedGroupID_ = draft_.groups.front().id;
	loaded_ = true;
	statusMessage.clear();
	return true;
}

bool ParticleEffectEditSession::SaveEffect(const EditorToolContext& context, std::string& statusMessage) {

	if (!loaded_ || !editingID_ || !context.toolContext.assetDatabase) {
		return false;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(editingID_);
	if (path.empty()) {

		statusMessage = "保存先のパスを解決できません";
		return false;
	}
	FinishEditing();
	if (!JsonAdapter::SaveCanonical(path, ToJson(draft_))) {
		statusMessage = "エフェクトの保存に失敗しました";
		return false;
	}
	savedDraft_ = draft_;
	dirty_ = false;
	context.toolContext.assetDatabase->NotifyContentChanged(editingID_);
	ParticleEffectEditBridge::GetInstance().Remove(editingID_);
	statusMessage = "保存しました: " + path.filename().string();
	return true;
}

bool ParticleEffectEditSession::CreateEffect(const EditorToolContext& context, std::string& statusMessage, const std::string& createName) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || createName.empty()) {
		return false;
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
	if (createName.find_first_of("/\\:*?\"<>|") != std::string::npos ||
		createName == "." || createName == ".." || std::filesystem::exists(path, ec)) {
		statusMessage = "作成名が無効か、同名のエフェクトが存在します";
		return false;
	}
	if (ec) {
		statusMessage = "作成先を確認できません";
		return false;
	}
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec || !JsonAdapter::SaveCanonical(path, ToJson(asset))) {
		statusMessage = "エフェクトの作成に失敗しました";
		return false;
	}

	const AssetID assetID = assetDatabase->ImportOrGet(logical, AssetType::ParticleEffect);
	if (!assetID) {

		statusMessage = "エフェクトの作成に失敗しました";
		return false;
	}
	statusMessage = "作成しました: " + logical;
	return LoadEffect(context, assetID, statusMessage);
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
