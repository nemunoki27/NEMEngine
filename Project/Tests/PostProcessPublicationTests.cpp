#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetPublication.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Editor/Tools/Builtin/RenderFeatures/RenderFeatureEditSession.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <algorithm>
#include <array>
#include <utility>

bool NEMTests::TestPostProcessPublication() {

	using namespace Engine;
	TestDirectory directory("PostProcessPublication", RuntimePaths::GetGameAssetsRoot() / "Shaders");
	std::array<PostProcessAssetSource, 2> sources;
	for (size_t index = 0; index < sources.size(); ++index) {
		auto& source = sources[index];
		source.name = index == 0 ? "Invert" : "Vignette";
		source.builtin = true;
		source.sourceShader = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".CS.hlsl"));
		source.shaderFile = source.sourceShader;
		source.shaderPath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".shader.json"));
		source.pipelinePath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".pipeline.json"));
		source.materialPath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".material.json"));
		if (!StorageFileUtility::WriteBytes(
				RuntimePaths::ResolveAssetPath(source.sourceShader), "[numthreads(1,1,1)] void main() {}")) {
			return false;
		}
	}
	const auto shaderPath = RuntimePaths::ResolveAssetPath(sources[0].shaderPath);
	const auto target = RuntimePaths::ResolveAssetPath(sources[1].materialPath);
	const nlohmann::json custom{{"name", "CustomShader"}, {"stages", nlohmann::json::array()}};
	const nlohmann::json previous{
		{"name", "PreviousMaterial"}, {"generated", true}, {"generatedBy", "PostProcessAssetGenerator"}, {"domain", "Compute"}};
	if (!JsonFile::Save(shaderPath, custom) || !JsonFile::Save(target, previous)) {
		return false;
	}
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const auto originalShader = database.ImportOrGet(sources[0].shaderPath, AssetType::Shader);
	const auto originalMaterial = database.ImportOrGet(sources[1].materialPath, AssetType::Material);
	const auto structure = database.GetStructureRevision();
	const auto content = database.GetContentRevision();
	const auto shaderRevision = StorageFileUtility::FileRevision(shaderPath);
	const auto* retainedShader = database.Find(originalShader);
	std::vector<AssetID> identifiers{{9, 9}};
	std::string diagnostic;
	{
		// 後半の保存失敗で前半の文書と索引も戻す
		TestFileReadLock lock(target);
		if (PostProcessAssetPublication::PublishBatch(database, sources, identifiers, diagnostic) || diagnostic.empty() ||
			identifiers != std::vector<AssetID>{{9, 9}} || database.Find(originalShader) != retainedShader ||
			database.GetStructureRevision() != structure || database.GetContentRevision() != content ||
			JsonFile::Load(target, false) != previous || StorageFileUtility::FileRevision(shaderPath) != shaderRevision ||
			std::filesystem::exists(RuntimePaths::ResolveAssetPath(sources[0].pipelinePath)) ||
			std::filesystem::exists(RuntimePaths::ResolveAssetPath(sources[0].materialPath))) {
			return false;
		}
	}
	// ロック解除後は同じ操作を再試行できる
	if (!PostProcessAssetPublication::PublishBatch(database, sources, identifiers, diagnostic) || identifiers.size() != 2 ||
		!diagnostic.empty() || identifiers[1] != originalMaterial ||
		StorageFileUtility::FileRevision(shaderPath) != shaderRevision || JsonFile::Load(shaderPath, false) != custom) {
		return false;
	}
	for (size_t index = 0; index < sources.size(); ++index) {
		const auto* pipeline = database.FindByPath(sources[index].pipelinePath);
		const auto* shader = database.FindByPath(sources[index].shaderPath);
		const auto* material = database.Find(identifiers[index]);
		if (!pipeline || !shader || !material || material->type != AssetType::Material) {
			return false;
		}
		const auto& dependencies = database.FindDependencies(material->guid);
		const auto& pipelineDependencies = database.FindDependencies(pipeline->guid);
		if (std::find(dependencies.begin(), dependencies.end(), pipeline->guid) == dependencies.end() ||
			std::find(pipelineDependencies.begin(), pipelineDependencies.end(), shader->guid) == pipelineDependencies.end()) {
			return false;
		}
	}
	// 標準値と既存の手書きShader参照を維持
	const auto first = JsonFile::Load(RuntimePaths::ResolveAssetPath(sources[0].materialPath), false);
	const auto second = JsonFile::Load(target, false);
	if (first.at("parameters").at("strength") != 1.0f || second.at("parameters").at("intensity") != 0.45f ||
		database.FindByPath(sources[0].shaderPath)->guid != originalShader) {
		return false;
	}
	const auto existing = PostProcessAssetGenerator::EnsureUserAsset(&database, sources[0].sourceShader);
	return existing == identifiers[0];
}

bool NEMTests::TestPostProcessLivePreview() {

	using namespace Engine;
	TestDirectory directory("PostProcessLivePreview", RuntimePaths::GetGameAssetsRoot());
	const auto firstPath = directory.GetPath() / "first.renderpasses.json";
	const auto secondPath = directory.GetPath() / "second.renderpasses.json";
	RenderPassesAsset first;
	first.name = "First";
	RenderFeaturePassSettings pass;
	pass.id = Engine::UUID{9101};
	pass.material = AssetID{9102, 9103};
	pass.parameterOverrides.Set("intensity", MaterialParameterValue{0.2f});
	pass.sceneInputs["gDepth"] = "Depth";
	pass.textureOverrides["gMask"] = AssetID{9104, 9105};
	pass.samplerOverrides["gSampler"].mipLODBias = 0.0f;
	first.passes.push_back(pass);
	RenderPassesAsset second = first;
	second.name = "Second";
	const auto saved = ToJson(first);
	if (!JsonFile::Save(firstPath, saved) || !JsonFile::Save(secondPath, ToJson(second))) {
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
	RenderPipelineRunner pipeline;
	RenderAssetLibrary& library = pipeline.GetRenderAssetLibrary();
	library.Init(&database);
	EditorPanelContext panel;
	panel.renderPipeline = &pipeline;
	EditorToolContext context;
	context.panelContext = &panel;
	context.toolContext.assetDatabase = &database;
	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	const auto previousPath = service.GetCurrentPath();
	auto previousProfile = service.GetProfile();
	const bool previousDirty = service.IsDirty();
	RenderFeatureEditSession session;
	const bool passed = [&]() {
		session.SelectProfile(context, firstID);
		if (session.HasError()) {
			return false;
		}
		session.SynchronizePreview(context);
		const auto* initial = library.LoadRenderPasses(firstID);
		if (!initial || !library.HasPreviewRenderPasses(firstID)) {
			return false;
		}
		RenderFeatureProfileRuntime runtime;
		runtime.Rebuild(ToRuntimeProfile(*initial));
		const auto retainedPlan = runtime.GetPlan(pass.anchor, RenderViewKind::Game);
		if (!retainedPlan->IsValid() || retainedPlan->nodes.size() != 1) {
			return false;
		}
		// 保存前の値と入力資源を描画側の新しい計画へ渡す
		for (float intensity : {0.0f, 0.6f, 1.0f}) {
			auto& edited = service.GetProfile().passes.front();
			edited.parameterOverrides.Set("intensity", MaterialParameterValue{intensity});
			edited.sceneInputs["gDepth"] = "GBufferNormal";
			edited.textureOverrides["gMask"] = AssetID{9106, 9107};
			edited.samplerOverrides["gSampler"].mipLODBias = 1.0f;
			session.SetDirty();
			const auto revision = library.GetRenderPassesRevision(firstID);
			session.SynchronizePreview(context);
			const auto* preview = library.LoadRenderPasses(firstID);
			if (!preview || library.GetRenderPassesRevision(firstID) == revision) {
				return false;
			}
			runtime.Rebuild(ToRuntimeProfile(*preview));
			const auto plan = runtime.GetPlan(pass.anchor, RenderViewKind::Game);
			if (!plan->IsValid() || plan->nodes.size() != 1) {
				return false;
			}
			const auto& applied = *plan->nodes.front().pass;
			const auto* value = applied.parameterOverrides.FindByName("intensity");
			if (!value || std::get<float>(value->value) != intensity ||
				applied.sceneInputs.at("gDepth") != "GBufferNormal" ||
				applied.textureOverrides.at("gMask") != AssetID{9106, 9107} ||
				applied.samplerOverrides.at("gSampler").mipLODBias != 1.0f ||
				JsonFile::Load(firstPath, false) != saved) {
				return false;
			}
			// 編集がないframeでは再公開せず、使用中の旧計画も保つ
			const auto publishedRevision = library.GetRenderPassesRevision(firstID);
			session.SynchronizePreview(context);
			if (library.GetRenderPassesRevision(firstID) != publishedRevision ||
				std::get<float>(retainedPlan->nodes.front().pass->parameterOverrides.FindByName("intensity")->value) != 0.2f ||
				retainedPlan->nodes.front().pass->sceneInputs.at("gDepth") != "Depth" ||
				retainedPlan->nodes.front().pass->textureOverrides.at("gMask") != AssetID{9104, 9105}) {
				return false;
			}
		}
		// 再読込は未保存値を破棄して描画側も保存値へ戻す
		session.Reload();
		session.SynchronizePreview(context);
		const auto* reloaded = library.LoadRenderPasses(firstID);
		if (session.HasError() || service.IsDirty() || !reloaded ||
			std::get<float>(reloaded->passes.front().parameterOverrides.FindByName("intensity")->value) != 0.2f ||
			reloaded->passes.front().sceneInputs.at("gDepth") != "Depth" ||
			reloaded->passes.front().textureOverrides.at("gMask") != AssetID{9104, 9105} ||
			reloaded->passes.front().samplerOverrides.at("gSampler").mipLODBias != 0.0f) {
			return false;
		}
		// 別の設定へ切り替えた後に旧Cameraへ編集を残さない
		session.SelectProfile(context, secondID);
		session.SynchronizePreview(context);
		if (session.HasError() || library.HasPreviewRenderPasses(firstID) || !library.HasPreviewRenderPasses(secondID)) {
			return false;
		}
		const auto firstRevision = library.GetRenderPassesRevision(firstID);
		service.GetProfile().passes.front().parameterOverrides.Set("intensity", MaterialParameterValue{2.0f});
		session.SetDirty();
		session.SynchronizePreview(context);
		const auto* retained = library.LoadRenderPasses(firstID);
		return retained && library.GetRenderPassesRevision(firstID) == firstRevision &&
			std::get<float>(retained->passes.front().parameterOverrides.FindByName("intensity")->value) == 0.2f;
	}();
	// 次の検証へ未保存の構成を持ち越さない
	library.DiscardPreviewRenderPasses(firstID);
	library.DiscardPreviewRenderPasses(secondID);
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
