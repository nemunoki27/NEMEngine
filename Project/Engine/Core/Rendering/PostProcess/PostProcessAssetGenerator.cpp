#include "PostProcessAssetGenerator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

// json
#include <json.hpp>

//============================================================================
//	PostProcessAssetGenerator classMethods
//============================================================================

namespace {

	constexpr const char* kGeneratedBy = "PostProcessAssetGenerator";

	struct BuiltinPostProcessSource {

		std::string baseName;
		std::string folder;
		std::string sourceShaderPath;
		std::filesystem::path hlslPath;
		std::filesystem::path shaderJsonPath;
		std::filesystem::path pipelineJsonPath;
		std::filesystem::path materialJsonPath;
	};

	bool EndsWith(std::string_view text, std::string_view suffix) {

		if (text.size() < suffix.size()) {
			return false;
		}
		return text.substr(text.size() - suffix.size()) == suffix;
	}

	std::string ToLower(std::string text) {

		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	std::string ToDisplayName(const std::string& baseName, const std::string& folder) {

		if (!folder.empty()) {
			const std::filesystem::path folderPath(folder);
			std::string name = folderPath.filename().string();
			if (!name.empty()) {
				return name;
			}
		}
		if (baseName.empty()) {
			return {};
		}

		std::string result = baseName;
		result.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(result.front())));
		return result;
	}

	nlohmann::json MakeDefaultParameters(const std::string& baseName) {

		const std::string key = ToLower(baseName);
		if (key == "invert" || key == "grayscale") {
			return { { "strength", 1.0f } };
		}
		if (key == "vignette") {
			return { { "intensity", 0.45f }, { "radius", 0.75f }, { "softness", 0.35f } };
		}
		if (key == "blurhorizontal" || key == "blurvertical") {
			return { { "radius", 1.0f } };
		}
		if (key == "bloomprefilter") {
			return { { "threshold", 1.0f }, { "knee", 0.5f }, { "intensity", 1.0f } };
		}
		if (key == "bloomcomposite") {
			return { { "intensity", 0.75f } };
		}
		return nlohmann::json::object();
	}

	nlohmann::json AddGeneratedMetadata(nlohmann::json data, const BuiltinPostProcessSource& source) {

		data["generated"] = true;
		data["generatedBy"] = kGeneratedBy;
		data["sourceShader"] = source.sourceShaderPath;
		return data;
	}

	nlohmann::json MakeShaderJson(const BuiltinPostProcessSource& source) {

		return AddGeneratedMetadata(nlohmann::json{
			{ "name", source.baseName + "Shader" },
			{ "stages", nlohmann::json::array({
				{
					{ "stage", "CS" },
					{ "file", "Builtin/PostProcess/" + source.folder + "/" + source.baseName + ".CS.hlsl" },
					{ "entry", "main" },
					{ "profile", "cs_6_0" }
				}
			}) }
			}, source);
	}

	nlohmann::json MakePipelineJson(const BuiltinPostProcessSource& source) {

		return AddGeneratedMetadata(nlohmann::json{
			{ "name", source.baseName + "Pipeline" },
			{ "variants", nlohmann::json::array({
				{
					{ "kind", "Compute" },
					{ "pipelineType", "Compute" },
					{ "shader", "Engine/Assets/Shaders/Builtin/PostProcess/" + source.folder + "/" + source.baseName + ".shader.json" }
				}
			}) }
			}, source);
	}

	nlohmann::json MakeMaterialJson(const BuiltinPostProcessSource& source) {

		return AddGeneratedMetadata(nlohmann::json{
			{ "name", source.baseName + "Material" },
			{ "domain", "Compute" },
			{ "passes", nlohmann::json::array({
				{
					{ "passName", "PostProcess" },
					{ "pipeline", "Engine/Assets/Pipelines/Builtin/PostProcess/" + source.folder + "/" + source.baseName + ".pipeline.json" },
					{ "preferredVariant", "Compute" }
				}
			}) },
			{ "parameters", MakeDefaultParameters(source.baseName) }
			}, source);
	}

	bool IsGeneratedByThisTool(const nlohmann::json& data) {

		return data.value("generated", false) &&
			data.value("generatedBy", std::string{}) == kGeneratedBy;
	}

	bool IsLegacyBuiltinGenerated(const nlohmann::json& data) {

		// 以前の自動生成版はgeneratedメタデータを持たない。
		// 手動asset保護を優先し、最低限の既定フィールドだけのものに限定して移行する。
		if (!data.is_object() || data.contains("generated") || data.contains("generatedBy")) {
			return false;
		}
		if (data.contains("stages") && data["stages"].is_array()) {
			return true;
		}
		if (data.contains("variants") && data["variants"].is_array()) {
			return true;
		}
		if (data.contains("domain") && data.contains("passes")) {
			return true;
		}
		return false;
	}

	bool CanOverwriteAsset(const std::filesystem::path& path) {

		if (!std::filesystem::exists(path)) {
			return true;
		}

		std::ifstream ifs(path, std::ios::binary);
		nlohmann::json oldData = nlohmann::json::parse(ifs, nullptr, false);
		if (oldData.is_discarded()) {
			return false;
		}
		return IsGeneratedByThisTool(oldData) || IsLegacyBuiltinGenerated(oldData);
	}

	void WriteGeneratedJson(const std::filesystem::path& path, const nlohmann::json& data) {

		std::filesystem::create_directories(path.parent_path());
		const std::string newText = data.dump(4);

		if (std::filesystem::exists(path)) {

			if (!CanOverwriteAsset(path)) {
				Engine::Logger::Output(Engine::LogType::Engine, "[PostProcessAssetGenerator] skip custom asset. path=" + path.generic_string());
				return;
			}

			std::ifstream ifs(path, std::ios::binary);
			const std::string oldText((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
			if (oldText == newText) {
				return;
			}
		}

		std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
		ofs << newText;
		Engine::Logger::Output(Engine::LogType::Engine, "[PostProcessAssetGenerator] write asset. path=" + path.generic_string());
	}

	std::vector<BuiltinPostProcessSource> GatherPostProcessSources(const std::filesystem::path& assetRoot) {

		std::vector<BuiltinPostProcessSource> sources{};
		const std::filesystem::path shaderRoot = assetRoot / "Shaders/Builtin/PostProcess";
		if (!std::filesystem::exists(shaderRoot)) {
			return sources;
		}

		for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::string fileName = entry.path().filename().string();
			if (!EndsWith(fileName, ".CS.hlsl")) {
				continue;
			}

			std::filesystem::path relative = std::filesystem::relative(entry.path(), shaderRoot);
			std::filesystem::path relativeFolder = relative.parent_path();
			std::string baseName = fileName.substr(0, fileName.size() - std::string(".CS.hlsl").size());
			std::string folder = relativeFolder.generic_string();
			if (folder.empty()) {
				folder = baseName;
			}

			BuiltinPostProcessSource source{};
			source.baseName = baseName;
			source.folder = folder;
			source.sourceShaderPath = "Engine/Assets/Shaders/Builtin/PostProcess/" + relative.generic_string();
			source.hlslPath = entry.path();
			source.shaderJsonPath = shaderRoot / relativeFolder / (baseName + ".shader.json");
			source.pipelineJsonPath = assetRoot / "Pipelines/Builtin/PostProcess" / relativeFolder / (baseName + ".pipeline.json");
			source.materialJsonPath = assetRoot / "Materials/Builtin/PostProcess" / relativeFolder / (baseName + ".material.json");
			sources.emplace_back(std::move(source));
		}
		return sources;
	}

	void AddMaterialAlias(std::unordered_map<std::string, Engine::AssetID>& table,
		const std::string& alias, Engine::AssetID materialID) {

		if (!alias.empty() && materialID) {
			table[alias] = materialID;
		}
	}
}

void Engine::PostProcessAssetGenerator::EnsureBuiltinAssets(AssetDatabase* database) {

	if (!database) {
		return;
	}
	if (generated_ && database_ == database) {
		return;
	}

	materialTable_.clear();
	database_ = database;
	generated_ = true;

	const std::filesystem::path assetRoot = RuntimePaths::GetEngineAssetsRoot();
	const auto sources = GatherPostProcessSources(assetRoot);
	for (const BuiltinPostProcessSource& source : sources) {

		WriteGeneratedJson(source.shaderJsonPath, MakeShaderJson(source));
		WriteGeneratedJson(source.pipelineJsonPath, MakePipelineJson(source));
		WriteGeneratedJson(source.materialJsonPath, MakeMaterialJson(source));

		const std::string materialAssetPath =
			"Engine/Assets/Materials/Builtin/PostProcess/" + source.folder + "/" + source.baseName + ".material.json";
		const AssetID materialID = database->ImportOrGet(materialAssetPath, AssetType::Material);
		if (!materialID) {
			continue;
		}

		const std::string displayName = ToDisplayName(source.baseName, source.folder);
		AddMaterialAlias(materialTable_, source.baseName, materialID);
		AddMaterialAlias(materialTable_, displayName, materialID);
		AddMaterialAlias(materialTable_, source.baseName + "Material", materialID);
		AddMaterialAlias(materialTable_, displayName + "Material", materialID);
	}
}

Engine::AssetID Engine::PostProcessAssetGenerator::FindBuiltinMaterial(std::string_view name) const {

	auto found = materialTable_.find(std::string(name));
	return found == materialTable_.end() ? AssetID{} : found->second;
}

void Engine::PostProcessAssetGenerator::Clear() {

	materialTable_.clear();
	database_ = nullptr;
	generated_ = false;
}
