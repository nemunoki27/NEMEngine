#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphPublication.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphAssetAuthoring.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

// c++
#include <iostream>

namespace {

	//============================================================================
	//	ShaderGraphTestArtifacts class
	//	検証で生成したShaderだけを終了時に回収する
	//============================================================================
	class ShaderGraphTestArtifacts {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ShaderGraphTestArtifacts(Engine::AssetID graphID)
			: root_(Engine::RuntimePaths::GetLibraryPath("ShaderGraph") / Engine::ToString(graphID)) {}
		~ShaderGraphTestArtifacts() {

			if (Engine::StorageFileUtility::IsInside(root_, Engine::RuntimePaths::GetLibraryPath("ShaderGraph"))) {
				std::error_code error;
				std::filesystem::remove_all(root_, error);
				if (error) {
					std::cerr << "ShaderGraph test cleanup failed: " << error.message() << '\n';
				}
			}
		}
		ShaderGraphTestArtifacts(const ShaderGraphTestArtifacts&) = delete;
		ShaderGraphTestArtifacts& operator=(const ShaderGraphTestArtifacts&) = delete;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 検証用Graphの生成先
		std::filesystem::path root_;
	};
}

bool NEMTests::TestShaderGraphPublication() {

	using namespace Engine;
	TestDirectory directory("ShaderGraphPublication", RuntimePaths::GetGameAssetsRoot() / "Materials");
	const auto graphPath = directory.GetPath() / "Fixture.shadergraph.json";
	const auto materialPath = directory.GetPath() / "Fixture.material.json";
	auto graph = CreateDefaultSurfaceShaderGraph("Fixture", ShaderGraphTarget::Sprite);
	const nlohmann::json initialGraph = ToJson(graph);
	const nlohmann::json initialMaterial = {{"name", "retained"}, {"passes", nlohmann::json::array()}};
	if (!JsonFile::Save(graphPath, initialGraph) || !JsonFile::Save(materialPath, initialMaterial)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({RuntimePaths::GetEngineAssetsRoot() / "Shaders", directory.GetPath()})) {
		return false;
	}
	const AssetID graphID = database.ImportOrGet(RuntimePaths::ToAssetPath(graphPath), AssetType::ShaderGraph);
	const AssetID originalMaterialID = database.ImportOrGet(RuntimePaths::ToAssetPath(materialPath), AssetType::Material);
	if (!graphID || !originalMaterialID) {
		return false;
	}
	ShaderGraphTestArtifacts artifacts(graphID);
	EditorToolContext context;
	context.toolContext.assetDatabase = &database;
	std::vector<ShaderGraphDiagnostic> diagnostics;
	std::string status;
	AssetID materialID{1, 2};
	graph.name = "Changed";
	const auto graphRevision = StorageFileUtility::FileRevision(graphPath);
	const auto materialRevision = StorageFileUtility::FileRevision(materialPath);
	const auto contentRevision = database.GetContentRevision();
	const auto lifetime = database.GetCacheLifetime();
	{
		// 後段のGraph保存失敗では生成Materialも元へ戻す
		TestFileReadLock lock(graphPath);
		if (ShaderGraphPublication::CompileAndPublish(
				context, database, graph, graphID, graphPath, materialID, diagnostics, status) ||
			status.empty() || materialID != AssetID{1, 2}) {
			return false;
		}
	}
	if (StorageFileUtility::FileRevision(graphPath) != graphRevision ||
		StorageFileUtility::FileRevision(materialPath) != materialRevision ||
		database.GetContentRevision() != contentRevision || lifetime.expired()) {
		return false;
	}

	// 基底Pipelineが欠ける成果物を成功扱いにしない
	AssetDatabase missingPipelines;
	missingPipelines.Init();
	ShaderGraphArtifact incomplete;
	incomplete.root = "Retained";
	incomplete.opaqueShaderID = AssetID{101, 102};
	if (ShaderGraphArtifactCache::Compile(
			CreateDefaultPostProcessShaderGraph("Missing"), graphID, incomplete, &missingPipelines) ||
		ShaderGraphArtifactCache::Compile(
			CreateDefaultSurfaceShaderGraph("Missing", ShaderGraphTarget::Text), graphID, incomplete, &missingPipelines) ||
		incomplete.root != "Retained" || incomplete.opaqueShaderID != AssetID{101, 102}) {
		return false;
	}

	ShaderGraphArtifact retained;
	if (!ShaderGraphArtifactCache::Compile(graph, graphID, retained, &database)) {
		return false;
	}
	// 後段の保存も実行させて複数ソースの復元を確認
	if (!StorageFileUtility::WriteBytes(
			retained.transparentPixelPath, retained.compileOutput.transparentPixelHLSL + "\n// 保持検証\n")) {
		return false;
	}
	const auto surfaceRevision = StorageFileUtility::FileRevision(retained.surfacePath);
	const auto transparentRevision = StorageFileUtility::FileRevision(retained.transparentPixelPath);
	const auto previousSurface = retained.compileOutput.surfaceHLSL;
	const AssetID previousShader = retained.opaqueShaderID;
	auto changedGraph = graph;
	changedGraph.nodes[1].value.value = Color4(0.2f, 0.4f, 0.6f, 1.0f);
	{
		TestFileReadLock lock(retained.transparentPixelPath);
		if (ShaderGraphArtifactCache::Compile(changedGraph, graphID, retained, &database)) {
			return false;
		}
	}
	if (StorageFileUtility::FileRevision(retained.surfacePath) != surfaceRevision ||
		StorageFileUtility::FileRevision(retained.transparentPixelPath) != transparentRevision ||
		retained.opaqueShaderID != previousShader || retained.compileOutput.surfaceHLSL != previousSurface) {
		return false;
	}
	bool recoveredSources = false;
	std::filesystem::path sourceRecovery;
	for (const auto& entry : std::filesystem::directory_iterator(retained.root / "Recovery")) {
		nlohmann::json operation;
		if (entry.is_directory() && JsonFile::TryLoad(entry.path() / "operation.json", operation) &&
			operation.value("state", "") == "recovered" && operation["files"].size() >= 2) {
			recoveredSources = true;
			sourceRecovery = entry.path();
		}
	}
	if (!recoveredSources) {
		return false;
	}
	// 前回の生成が途中で中断した状態を再現する
	nlohmann::json interrupted;
	if (!JsonFile::TryLoad(sourceRecovery / "operation.json", interrupted)) {
		return false;
	}
	interrupted["state"] = "pending";
	// 作業ファイルの配置に依存せず生成内容から中断を再現
	const auto changedSource = ShaderGraphCompiler::Compile(changedGraph, retained.surfacePath.filename().string());
	if (!changedSource.Succeeded()) {
		return false;
	}
	if (!JsonFile::Save(sourceRecovery / "operation.json", interrupted) ||
		!StorageFileUtility::WriteBytes(retained.surfacePath, changedSource.surfaceHLSL) ||
		StorageFileUtility::FileRevision(retained.surfacePath) != interrupted["files"][0]["after"].get<std::string>()) {
		return false;
	}
	// 中断後の外部変更は上書きせず旧成果物を維持する
	if (!StorageFileUtility::WriteBytes(retained.surfacePath, "// 外部編集の保持検証\n")) {
		return false;
	}
	const auto externalRevision = StorageFileUtility::FileRevision(retained.surfacePath);
	if (ShaderGraphArtifactCache::Compile(changedGraph, graphID, retained, &database) ||
		StorageFileUtility::FileRevision(retained.surfacePath) != externalRevision ||
		retained.compileOutput.surfaceHLSL != previousSurface || retained.opaqueShaderID != previousShader) {
		return false;
	}
	// 外部変更を解消した後は復旧と再生成を続行する
	if (!StorageFileUtility::WriteBytes(retained.surfacePath, changedSource.surfaceHLSL) ||
		!ShaderGraphArtifactCache::Compile(graph, graphID, retained, &database) ||
		StorageFileUtility::FileRevision(retained.surfacePath) != surfaceRevision ||
		!JsonFile::TryLoad(sourceRecovery / "operation.json", interrupted) || interrupted["state"] != "recovered") {
		return false;
	}
	// 解析失敗の診断を成果物と分けて返す
	auto invalidGraph = graph;
	invalidGraph.outputNode = {};
	if (ShaderGraphArtifactCache::Compile(invalidGraph, graphID, retained, &database, &diagnostics) || diagnostics.empty() ||
		retained.compileOutput.surfaceHLSL != previousSurface ||
		!ShaderGraphArtifactCache::Compile(changedGraph, graphID, retained, &database) ||
		retained.compileOutput.surfaceHLSL == previousSurface ||
		StorageFileUtility::FileRevision(retained.surfacePath) == surfaceRevision) {
		return false;
	}

	// 成功時に既存GUIDを保持して両文書を公開する
	if (!ShaderGraphPublication::CompileAndPublish(
			context, database, graph, graphID, graphPath, materialID, diagnostics, status) ||
		materialID != originalMaterialID || !lifetime.expired() || database.GetContentRevision(graphID) == 0 ||
		database.GetContentRevision(materialID) == 0) {
		return false;
	}
	nlohmann::json savedGraph;
	nlohmann::json savedMaterial;
	if (!JsonFile::TryLoad(graphPath, savedGraph) || savedGraph["name"] != "Changed" ||
		!JsonFile::TryLoad(materialPath, savedMaterial) || savedMaterial["shaderGraph"] != ToAssetReferenceJson(graphID)) {
		return false;
	}

	// Graphだけの保存でも参照と更新番号を確定する
	const auto materialHash = StorageFileUtility::FileRevision(materialPath);
	const auto graphContent = database.GetContentRevision(graphID);
	graph.name = "SavedOnly";
	if (ShaderGraphPublication::SaveGraph(database, graph, graphPath, graphID, status) != graphID ||
		database.GetContentRevision(graphID) <= graphContent ||
		StorageFileUtility::FileRevision(materialPath) != materialHash) {
		return false;
	}

	// 新規作成の登録失敗では文書を残さない
	ShaderGraphCreationRequest request;
	const auto newPath = directory.GetPath() / "New.shadergraph.json";
	request.path = RuntimePaths::ToAssetPath(newPath);
	request.target = ShaderGraphTarget::Sprite;
	const auto blockedMeta = std::filesystem::path(newPath.string() + ".meta");
	std::filesystem::create_directory(blockedMeta);
	if (ShaderGraphAssetAuthoring::Create(database, request, status) || std::filesystem::exists(newPath) ||
		database.FindByPath(request.path) || status.empty()) {
		return false;
	}
	std::filesystem::remove(blockedMeta);
	const auto created = ShaderGraphAssetAuthoring::Create(database, request, status);
	if (!created || !database.Find(created) || !std::filesystem::exists(newPath)) {
		return false;
	}

	// 作成として渡した既存Graphは上書きしない
	const auto retainedHash = StorageFileUtility::FileRevision(graphPath);
	return !ShaderGraphPublication::SaveGraph(database, graph, graphPath, {}, status) && !status.empty() &&
		   StorageFileUtility::FileRevision(graphPath) == retainedHash;
}
