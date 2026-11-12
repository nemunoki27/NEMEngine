#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/ScopedTransaction.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureReflectionCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Tools/Builtin/RenderFeatures/RenderFeatureEditSession.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <memory>
#include <stdexcept>
#include <utility>


bool NEMTests::TestEditorTransactionRollback() {

	// 途中失敗で入出力と階層をまとめて戻す
	Engine::RenderFeatureProfileAsset profile;
	profile.name = "Before";
	Engine::RenderFeaturePassSettings pass;
	pass.id = Engine::UUID{8001};
	profile.passes.push_back(pass);
	try {
		Engine::ScopedTransaction transaction(profile);
		profile.name = "After";
		profile.passes.clear();
		throw std::runtime_error("編集途中の失敗を検証");
	} catch (const std::runtime_error&) {
	}
	if (profile.name != "Before" || profile.passes.size() != 1 || profile.passes[0].id != pass.id) {
		return false;
	}
	// 確定後の処理終了では変更を維持する
	{
		Engine::ScopedTransaction transaction(profile);
		profile.name = "Committed";
		transaction.Commit();
	}
	return profile.name == "Committed" && profile.passes.size() == 1;
}

bool NEMTests::TestRenderPassesReflectionCache() {

	using namespace Engine;
	RenderFeatureReflectionCache cache;
	const AssetID first{9001, 9002};
	const AssetID second{9003, 9004};
	ShaderConstantBufferVariable variable{};
	variable.name = "First";
	ShaderResourceBinding resource{};
	resource.name = "Texture";
	ShaderResourceBinding sampler{};
	sampler.name = "Sampler";
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {resource}, {sampler});
	cache.CacheReflection(first, MaterialPassKind::Masked, {variable}, {}, {});
	cache.CacheReflection(second, MaterialPassKind::Draw, {}, {resource}, {});
	const auto* variables = cache.FindReflectionVariables(first, MaterialPassKind::Draw);
	const auto* resources = cache.FindReflectionResources(first, MaterialPassKind::Draw);
	const auto* samplers = cache.FindReflectionSamplers(first, MaterialPassKind::Draw);
	if (!variables || variables->size() != 1 || !resources || resources->size() != 1 || !samplers || samplers->size() != 1) {
		return false;
	}
	// 同じShaderの全種類を入れ替える
	variable.name = "Reloaded";
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {}, {});
	if (variables != cache.FindReflectionVariables(first, MaterialPassKind::Draw) || variables->at(0).name != "Reloaded" ||
		!resources->empty() || !samplers->empty()) {
		return false;
	}
	// 同じ公開世代を再利用し、新しいShaderだけを差し替える
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {}, {}, 123);
	const auto* data = cache.FindReflectionVariables(first, MaterialPassKind::Draw)->data();
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {}, {}, 123);
	if (!cache.HasReflection(first, MaterialPassKind::Draw, 123) || cache.HasReflection(first, MaterialPassKind::Draw, 124) ||
		cache.FindReflectionVariables(first, MaterialPassKind::Draw)->data() != data) return false;
	variable.name = "NextGeneration";
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {resource}, {}, 124);
	if (!cache.HasReflection(first, MaterialPassKind::Draw, 124) || variables->front().name != "NextGeneration" ||
		resources->size() != 1) return false;
	// Materialの削除は他のMaterialへ影響させない
	cache.ClearReflection(first);
	if (cache.FindReflectionVariables(first, MaterialPassKind::Draw) ||
		cache.FindReflectionVariables(first, MaterialPassKind::Masked) ||
		!cache.FindReflectionResources(second, MaterialPassKind::Draw)) {
		return false;
	}
	cache.ClearReflectionCache();
	return !cache.FindReflectionResources(second, MaterialPassKind::Draw);
}

bool NEMTests::TestRenderPassesSelectionRequests() {

	using namespace Engine;
	TestDirectory directory("RenderPassesSelection", RuntimePaths::GetGameAssetsRoot());
	const auto firstPath = directory.GetPath() / "first.renderpasses.json";
	const auto secondPath = directory.GetPath() / "second.renderpasses.json";
	RenderPassesAsset first;
	first.name = "First";
	RenderPassesAsset second;
	second.name = "Second";
	if (!JsonFile::Save(firstPath, ToJson(first)) || !JsonFile::Save(secondPath, ToJson(second))) {
		return false;
	}
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const AssetID firstID = database.ImportOrGet(RuntimePaths::ToAssetPath(firstPath), AssetType::RenderPasses);
	const AssetID secondID = database.ImportOrGet(RuntimePaths::ToAssetPath(secondPath), AssetType::RenderPasses);
	if (!firstID || !secondID) {
		return false;
	}
	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	const auto previousPath = service.GetCurrentPath();
	auto previousProfile = service.GetProfile();
	const bool previousDirty = service.IsDirty();
	EditorToolContext context;
	context.toolContext.assetDatabase = &database;
	RenderFeatureEditSession session;
	const bool passed = [&]() {
		// 選択解除も更新要求として一度だけ適用する
		session.RequestProfile(firstID);
		if (!session.Tick(context.toolContext) || session.GetProfileID() != firstID || service.GetProfile().name != "First") {
			return false;
		}
		service.MarkDirty();
		session.RequestProfile({});
		if (!session.Tick(context.toolContext) || session.GetProfileID() || !service.GetCurrentPath().empty() ||
			service.IsDirty() || !service.GetProfile().passes.empty()) {
			return false;
		}
		const auto generation = service.GetRuntimeGeneration();
		if (session.Tick(context.toolContext) || service.GetRuntimeGeneration() != generation) {
			return false;
		}
		// 確認後の直接選択を古い予約で上書きしない
		session.RequestProfile(firstID);
		session.SelectProfile(context, secondID);
		if (session.Tick(context.toolContext) || session.GetProfileID() != secondID || service.GetProfile().name != "Second") {
			return false;
		}
		// 現在の選択に戻す操作で切替予約を取り消す
		service.MarkDirty();
		session.RequestProfile({});
		session.RequestProfile(secondID);
		if (session.Tick(context.toolContext) || !service.IsDirty() || session.GetProfileID() != secondID) {
			return false;
		}
		// 読込失敗で現在の選択と未保存値を失わない
		const auto previousGeneration = service.GetRuntimeGeneration();
		if (!JsonFile::Save(firstPath, nlohmann::json{{"name", 123}})) {
			return false;
		}
		session.RequestProfile(firstID);
		if (session.Tick(context.toolContext) || !session.HasError() || session.GetProfileID() != secondID ||
			service.GetProfile().name != "Second" || !service.IsDirty() ||
			service.GetRuntimeGeneration() != previousGeneration || session.Tick(context.toolContext)) {
			return false;
		}
		if (!JsonFile::Save(secondPath, nlohmann::json{{"name", 123}})) {
			return false;
		}
		session.Reload();
		if (!session.HasError() || !service.IsDirty() || service.GetProfile().name != "Second" ||
			service.GetRuntimeGeneration() != previousGeneration || service.SetActiveProfileAsset(firstID, nullptr)) {
			return false;
		}
		// 修復後の再読込でだけ編集を破棄する
		second.name = "Reloaded";
		if (!JsonFile::Save(secondPath, ToJson(second))) {
			return false;
		}
		session.Reload();
		return !session.HasError() && !service.IsDirty() && service.GetProfile().name == "Reloaded";
	}();
	// 他の検証へ編集用の状態を持ち越さない
	service.SetActiveProfilePath(previousPath);
	service.GetProfile() = std::move(previousProfile);
	if (previousDirty) {
		service.MarkDirty();
	} else {
		service.ClearDirty();
	}
	service.RebuildRuntime();
	return passed;
}
