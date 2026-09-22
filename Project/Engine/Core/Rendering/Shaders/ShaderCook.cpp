#include "ShaderCook.h"

//============================================================================
//	include
//============================================================================
#include "ShaderCookStorage.h"
#include "ShaderCookCompiler.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {

	using namespace Engine::ShaderCookStorage;

	std::filesystem::path NormalizePath(
		const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	std::wstring MakePathKey(const std::filesystem::path& path) {

		std::wstring key = NormalizePath(path).generic_wstring();
		std::transform(key.begin(), key.end(), key.begin(),
			[](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
		return key;
	}

}

//============================================================================
//	ShaderCook classMethods
//============================================================================
bool Engine::ShaderCook::Cook(const std::filesystem::path& manifestPath,
	const std::filesystem::path& outputRoot,
	ShaderCookResult& outResult, std::string& outError) {

	outResult = ShaderCookResult{};
	outError.clear();
	std::error_code outputError;
	const std::filesystem::path resolvedOutputRoot =
		std::filesystem::absolute(outputRoot, outputError);
	if (outputError || resolvedOutputRoot.empty()) {
		outError = "シェーダーCookの出力先が不正です";
		return false;
	}
	const nlohmann::json buildManifest = JsonAdapter::Load(manifestPath, true);
	if (!buildManifest.is_object() ||
		buildManifest.value("schemaVersion", 0u) != 2u ||
		!buildManifest.contains("files") ||
		!buildManifest["files"].is_array()) {
		outError = "製品ビルドのマニフェストが不正です";
		return false;
	}

	const std::filesystem::path gameRoot = Algorithm::PathFromUTF8(
		buildManifest.value("gameRoot", std::string{}));
	std::error_code ec;
	std::filesystem::current_path(gameRoot, ec);
	if (ec) {
		outError = "ゲームプロジェクトのフォルダーが見つかりません";
		return false;
	}
	RuntimePaths::Refresh();
	AssetDatabase database{};
	if (!database.Init() || !database.RebuildMeta()) {
		outError = "アセットデータベースの初期化に失敗しました";
		return false;
	}

	std::unordered_set<std::wstring> includedFiles;
	for (const nlohmann::json& item : buildManifest["files"]) {
		const std::filesystem::path source = Algorithm::PathFromUTF8(
			item.value("source", std::string{}));
		if (!source.empty()) {
			includedFiles.insert(MakePathKey(source));
		}
	}

	std::vector<const AssetMeta*> shaderMetas;
	std::vector<const AssetMeta*> graphMetas;
	// 必須の組み込みアセットと依存先が収集一覧から欠けていればCookを開始しない
	std::vector<AssetID> requiredAssets(
		std::begin(BuiltinAssets::Runtime::Assets), std::end(BuiltinAssets::Runtime::Assets));
	std::unordered_set<AssetID> checkedRequiredAssets;
	for (size_t index = 0; index < requiredAssets.size(); ++index) {
		const AssetID assetID = requiredAssets[index];
		if (!checkedRequiredAssets.insert(assetID).second) continue;
		const AssetMeta* meta = database.Find(assetID);
		if (meta && meta->assetPath.starts_with("Engine/Assets/Shaders/Builtin/Editor/")) continue;
		const std::filesystem::path path = database.ResolveFullPath(assetID);
		std::filesystem::path metaPath = path;
		metaPath += L".meta";
		if (!meta || !includedFiles.contains(MakePathKey(path)) ||
			!includedFiles.contains(MakePathKey(metaPath)) ||
			!std::filesystem::is_regular_file(path, ec) || ec) {
			outError = "製品に必須のアセットが含まれていません GUID=" + ToString(assetID) +
				" path=" + Algorithm::PathToUTF8(path);
			return false;
		}
		for (const AssetID dependency : database.FindDependencies(assetID)) {
			requiredAssets.push_back(dependency);
		}
	}
	for (const auto& [assetID, meta] : database.GetAssets()) {
		if (!includedFiles.contains(MakePathKey(database.ResolveFullPath(assetID)))) {
			continue;
		}
		if (meta.type == AssetType::Shader) {
			// HLSLソースは定義ファイルのステージからコンパイルする
			const std::string extension = Algorithm::ToLower(
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(meta.assetPath).extension()));
			if (extension == ".hlsl" || extension == ".hlsli") {
				continue;
			}
			shaderMetas.emplace_back(&meta);
		} else if (meta.type == AssetType::ShaderGraph) {
			graphMetas.emplace_back(&meta);
		}
	}
	const auto sortByPath =
		[](const AssetMeta* lhs, const AssetMeta* rhs) {
			return lhs->assetPath < rhs->assetPath;
		};
	std::sort(shaderMetas.begin(), shaderMetas.end(), sortByPath);
	std::sort(graphMetas.begin(), graphMetas.end(), sortByPath);
	for (const AssetID shaderID : BuiltinAssets::Shaders::FixedRuntime) {
		if (std::none_of(shaderMetas.begin(), shaderMetas.end(),
			[shaderID](const AssetMeta* meta) { return meta->guid == shaderID; })) {
			outError = "製品描画に必須のシェーダーが含まれていません: " + ToString(shaderID);
			return false;
		}
	}

	ShaderCookCompiler compiler{ database, resolvedOutputRoot };
	nlohmann::json cookedManifest = {
		{ "schemaVersion", kShaderCookSchemaVersion },
		{ "cookedOnly", true },
		{ "shaders", nlohmann::json::array() },
		{ "pipelines", nlohmann::json::array() },
	};
	const auto cookShader = [&](ShaderAsset shader,
		std::string_view sourceName) -> bool {

		return compiler.Cook(std::move(shader), sourceName, cookedManifest, outResult, outError);
	};

	for (const AssetMeta* meta : shaderMetas) {
		ShaderAsset shader{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(meta->guid), true), shader)) {
			outError = "シェーダーアセットの読み込みに失敗しました: " + meta->assetPath;
			return false;
		}
		shader.guid = meta->guid;
		if (!cookShader(std::move(shader), meta->assetPath)) {
			return false;
		}
	}
	for (const AssetMeta* meta : graphMetas) {
		ShaderGraphAsset graph{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(meta->guid), true), graph)) {
			outError = "Shader Graphの読み込みに失敗しました: " + meta->assetPath;
			return false;
		}
		ShaderGraphArtifact artifact{};
		if (!ShaderGraphArtifactCache::Compile(
			graph, meta->guid, artifact, &database)) {
			outError = "Shader Graphのコンパイルに失敗しました: " + meta->assetPath;
			return false;
		}
		if (artifact.opaqueShader.guid &&
			!cookShader(std::move(artifact.opaqueShader), meta->assetPath)) {
			return false;
		}
		if (artifact.transparentShader.guid &&
			!cookShader(std::move(artifact.transparentShader), meta->assetPath)) {
			return false;
		}
		if (artifact.depthShader.guid &&
			!cookShader(std::move(artifact.depthShader), meta->assetPath)) {
			return false;
		}
		if (artifact.pickingShader.guid &&
			!cookShader(std::move(artifact.pickingShader), meta->assetPath)) {
			return false;
		}
		if (artifact.computeShader.guid &&
			!cookShader(std::move(artifact.computeShader), meta->assetPath)) {
			return false;
		}
		if (artifact.rayTracingShader.guid &&
			!cookShader(std::move(artifact.rayTracingShader), meta->assetPath)) {
			return false;
		}
		for (const RenderPipelineAsset* pipeline : {
			&artifact.opaquePipeline,
			&artifact.transparentPipeline,
			&artifact.depthPipeline,
			&artifact.pickingPipeline,
			&artifact.computePipeline,
			&artifact.rayTracingPipeline,
			}) {
			if (!pipeline->guid) {
				continue;
			}
			nlohmann::json pipelineData = ToJson(*pipeline);
			pipelineData["guid"] = ToString(pipeline->guid);
			cookedManifest["pipelines"].push_back(std::move(pipelineData));
		}
	}
	if (outResult.stageCount == 0) {
		outError = "製品ビルドにシェーダーアセットが含まれていません";
		return false;
	}

	// 必須Pipelineの製品用ステージもCook結果と照合する
	std::unordered_set<AssetID> cookedShaders;
	for (const auto& shader : cookedManifest["shaders"]) {
		cookedShaders.insert(FromString32Hex(shader.at("asset").at("guid").get<std::string>()));
	}
	for (const auto& [assetID, meta] : database.GetAssets()) {

		if (!checkedRequiredAssets.contains(assetID) || meta.type != AssetType::RenderPipeline ||
			!includedFiles.contains(MakePathKey(database.ResolveFullPath(assetID)))) {
			continue;
		}
		RenderPipelineAsset pipeline{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(assetID), false), pipeline)) {
			outError = "配置するPipelineを読み込めません: " + meta.assetPath;
			return false;
		}
		for (const auto& variant : pipeline.variants) {
			const AssetMeta* shaderMeta = database.Find(variant.shader);
			if (shaderMeta && shaderMeta->assetPath.starts_with("Engine/Assets/Shaders/Builtin/Editor/")) {
				continue;
			}
			if (!cookedShaders.contains(variant.shader)) {
				outError = "PipelineのShaderがCookされていません: " + meta.assetPath +
					" Shader=" + ToString(variant.shader);
				return false;
			}
		}
	}
	if (!JsonAdapter::SaveCanonical(resolvedOutputRoot / "ShaderCookManifest.json",
		cookedManifest)) {
		outError = "シェーダーCookのマニフェストの書き込みに失敗しました";
		return false;
	}
	return true;
}
