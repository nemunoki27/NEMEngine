#include "PostProcessAssetGenerator.h"

//============================================================================
//	include
//============================================================================
#include "PostProcessAssetPublication.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <exception>
#include <filesystem>
#include <vector>

namespace {

	using namespace Engine;

	// 二重拡張子を除いた生成名を取得する
	std::string BaseName(const std::string& path) {

		const auto stem = Algorithm::PathFromUTF8(path).stem();
		return Algorithm::PathToUTF8(stem.stem());
	}

	// 元Shaderと同じフォルダーに生成先を揃える
	PostProcessAssetSource UserSource(const std::string& path) {

		const auto normalized = Algorithm::PathFromUTF8(path).lexically_normal();
		PostProcessAssetSource source;
		source.name = BaseName(path);
		source.sourceShader = Algorithm::ConvertString(normalized.generic_wstring());
		source.shaderFile = source.sourceShader;
		const auto parent = normalized.parent_path();
		source.shaderPath = Algorithm::ConvertString((parent / (source.name + ".shader.json")).generic_wstring());
		source.pipelinePath = Algorithm::ConvertString((parent / (source.name + ".pipeline.json")).generic_wstring());
		source.materialPath = Algorithm::ConvertString((parent / (source.name + ".material.json")).generic_wstring());
		return source;
	}

	// 標準HLSLの生成先を収集する
	std::vector<PostProcessAssetSource> GatherSources() {

		std::vector<PostProcessAssetSource> sources;
		const auto shaderRoot = RuntimePaths::GetEngineAssetsRoot() / "Shaders/Builtin/PostProcess";
		if (!std::filesystem::exists(shaderRoot)) {
			return sources;
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			const auto filename = Algorithm::PathToUTF8(entry.path().filename());
			if (!std::string_view(filename).ends_with(".CS.hlsl")) {
				continue;
			}
			const auto relative = std::filesystem::relative(entry.path(), shaderRoot);
			const auto folder = relative.parent_path();
			const auto name = BaseName(filename);
			const auto assetFolder = entry.path().parent_path();
			// 固定パスの手書きAssetは重複生成しない
			if (std::filesystem::exists(assetFolder / (name + ".shader.json")) &&
				std::filesystem::exists(assetFolder / (name + ".pipeline.json")) &&
				std::filesystem::exists(assetFolder / (name + ".material.json"))) {
				continue;
			}
			PostProcessAssetSource source;
			source.name = name;
			source.builtin = true;
			source.sourceShader =
				"Engine/Assets/Shaders/Builtin/PostProcess/" + Algorithm::ConvertString(relative.generic_wstring());
			source.shaderFile = "Builtin/PostProcess/" + Algorithm::ConvertString(relative.generic_wstring());
			source.shaderPath = "Engine/Assets/Shaders/Builtin/PostProcess/" +
								Algorithm::ConvertString((folder / (name + ".shader.json")).generic_wstring());
			source.pipelinePath = "Engine/Assets/Pipelines/Builtin/PostProcess/" +
								  Algorithm::ConvertString((folder / (name + ".pipeline.json")).generic_wstring());
			source.materialPath = "Engine/Assets/Materials/Builtin/PostProcess/" +
								  Algorithm::ConvertString((folder / (name + ".material.json")).generic_wstring());
			sources.push_back(std::move(source));
		}
		return sources;
	}

	// 失敗した生成を診断へ残す
	void ReportFailure(const PostProcessAssetSource& source, const std::string& diagnostic) {

		Logger::Output(LogType::Engine, spdlog::level::err, "[PostProcessAssetGenerator] Assetを生成できません path={} 内容={}",
			source.materialPath, diagnostic);
	}
}

//============================================================================
//	PostProcessAssetGenerator classMethods
//============================================================================
void Engine::PostProcessAssetGenerator::EnsureBuiltinAssets(AssetDatabase* database) {

	if (!database || (generated_ && database_ == database && !databaseLifetime_.expired() &&
						 structureRevision_ == database->GetStructureRevision())) {
		return;
	}
	try {
		// 生成途中の一覧を公開しない
		const auto sources = GatherSources();
		std::vector<AssetID> identifiers;
		std::string diagnostic;
		if (!PostProcessAssetPublication::PublishBatch(*database, sources, identifiers, diagnostic)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[PostProcessAssetGenerator] 標準Assetを生成できません 内容={}",
				diagnostic);
			return;
		}
		std::unordered_map<std::string, AssetID> candidate;
		for (size_t index = 0; index < sources.size(); ++index) {
			const auto& source = sources[index];
			const auto id = identifiers[index];
			const auto folder = Algorithm::PathFromUTF8(source.shaderFile).parent_path();
			const auto display =
				folder == std::filesystem::path("Builtin/PostProcess") ? source.name : Algorithm::PathToUTF8(folder.filename());
			candidate[source.name] = id;
			candidate[display] = id;
			candidate[source.name + "Material"] = id;
			candidate[display + "Material"] = id;
		}
		// 全効果の生成成功後に一覧を公開
		materialTable_.swap(candidate);
		database_ = database;
		databaseLifetime_ = database->GetCacheLifetime();
		structureRevision_ = database->GetStructureRevision();
		generated_ = true;
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::err, "[PostProcessAssetGenerator] 標準Assetの生成を中止しました 内容={}",
			error.what());
	}
}

Engine::AssetID Engine::PostProcessAssetGenerator::FindBuiltinMaterial(std::string_view name) const {

	if (databaseLifetime_.expired() || structureRevision_ != database_->GetStructureRevision()) {
		return {};
	}
	// 登録済みの標準効果を名前で取得
	const auto found = materialTable_.find(std::string(name));
	return found == materialTable_.end() ? AssetID{} : found->second;
}

bool Engine::PostProcessAssetGenerator::IsComputeShaderSourcePath(std::string_view path) {

	return std::string_view(Algorithm::ToLower(std::string(path))).ends_with(".cs.hlsl");
}

void Engine::PostProcessAssetGenerator::Clear() {

	// 一覧と生成状態を解除
	materialTable_.clear();
	database_ = nullptr;
	databaseLifetime_.reset();
	structureRevision_ = 0;
	generated_ = false;
}

Engine::AssetID Engine::PostProcessAssetGenerator::EnsureUserAsset(AssetDatabase* database, const std::string& path) {

	if (!database || !IsComputeShaderSourcePath(path)) {
		return {};
	}
	const auto source = UserSource(path);
	if (source.name.empty()) {
		return {};
	}
	if (const auto* existing = database->FindByPath(source.materialPath)) {
		return existing->guid;
	}
	// 全Assetの保存成功を呼出し元へ返す
	std::string diagnostic;
	const auto id = PostProcessAssetPublication::Publish(*database, source, diagnostic);
	if (!id) {
		ReportFailure(source, diagnostic);
	}
	return id;
}

Engine::AssetID Engine::PostProcessAssetGenerator::FindOrCreateMaterialForShader(
	AssetDatabase* database, const std::string& path) {

	if (!database || path.empty()) {
		return {};
	}
	auto source = UserSource(path);
	if (source.name.empty()) {
		return {};
	}
	if (const auto* existing = database->FindByPath(source.materialPath)) {
		return existing->guid;
	}
	// 既存Shaderを維持し、PipelineとMaterialだけを生成
	source.shaderPath = source.sourceShader;
	source.shaderFile.clear();
	std::string diagnostic;
	const auto id = PostProcessAssetPublication::Publish(*database, source, diagnostic);
	if (!id) {
		ReportFailure(source, diagnostic);
	}
	return id;
}
