#include "GameBuildAssetCollector.h"
#include "GameBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/Scene/Serialization/SceneStorageJournal.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>

// c++
#include <array>
#include <fstream>
#include <regex>
#include <sstream>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Engine {

	using namespace GameBuildUtility;
	using BuildFileEntry = GameBuildFileEntry;

	void GameBuildAssetCollector::InspectFile(const Engine::AssetMeta& meta, const std::filesystem::path& source) {

		const std::string extension = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(source.extension()));
		if (extension == ".json" || AssetTypeResolver::IsJsonAssetFile(meta.type, source)) {

			const nlohmann::json data = LoadJson(source);
			if (!data.is_discarded() && !data.is_null()) {
				if (meta.type == Engine::AssetType::Scene) {
					CollectExternalActors(meta, source, data);
				}
				InspectJson(data);
			}
		}
		if (extension == ".hlsl" || extension == ".hlsli") {
			CollectShaderIncludes(source);
		}
		if (meta.type == Engine::AssetType::Mesh) {
			CollectModelSidecars(source);
		}
	}

	void GameBuildAssetCollector::CollectExternalActors(
		const Engine::AssetMeta& sceneMeta, const std::filesystem::path& scenePath, const nlohmann::json& sceneData) {

		if (!sceneData.contains("ExternalActors") || !sceneData["ExternalActors"].is_array()) {
			return;
		}

		const auto issues = sceneStorage_->Validate(scenePath, sceneMeta.guid);
		if (!issues.empty()) {
			for (const auto& issue : issues) {
				errors_.push_back(issue.detail + " scene=" + sceneMeta.assetPath +
								  " path=" + Engine::Algorithm::PathToUTF8(issue.actorPath) +
								  " / Projectパネルのシーンデータ検証・修復を確認してください");
			}
			return;
		}
		const std::filesystem::path actorRoot = Engine::SceneAssetStorage::ResolveActorRoot(scenePath, sceneMeta.guid);
		for (const nlohmann::json& actorID : sceneData["ExternalActors"]) {

			if (!actorID.is_string() || !Engine::TryParseUUID16Hex(actorID.get<std::string>())) {
				errors_.push_back("ExternalActor IDが不正です: " + sceneMeta.assetPath);
				continue;
			}
			const std::filesystem::path actorPath = actorRoot / (actorID.get<std::string>() + ".actor.json");
			const std::string assetPath = Engine::RuntimePaths::ToAssetPath(actorPath);
			if (assetPath.empty() || !std::filesystem::is_regular_file(actorPath)) {
				errors_.push_back("ExternalActorが見つかりません: " + Engine::Algorithm::PathToUTF8(actorPath));
				continue;
			}

			AddFile(actorPath, ToBuildDestination(assetPath));
			const nlohmann::json actor = LoadJson(actorPath);
			if (actor.is_object()) {
				InspectJson(actor);
			}
		}
	}

	void GameBuildAssetCollector::InspectJson(const nlohmann::json& node) {

		// 構文解析をAssetDatabaseと共有し、製品収集の方針はここで適用する
		AssetDependencyScanner::IDReferences candidates;
		AssetDependencyScanner::PathReferences paths;
		AssetDependencyScanner::ScanReferences(node, candidates, paths, true);
		for (const auto& [id, type] : candidates) {
			if (type != AssetType::Unknown || database_.Find(id)) {
				AddAsset(id);
			}
		}
		for (const auto& [path, type] : paths) {
			if (const auto* meta = database_.FindByPath(path)) {
				AddAsset(meta->guid);
			} else if (type == AssetType::Shader && (StartsWith(path, "Engine/Assets/") || StartsWith(path, "GameAssets/") ||
														StartsWith(path, "package://"))) {
				AddLogicalFile(path);
			}
		}
	}

	void GameBuildAssetCollector::CollectShaderIncludes(const std::filesystem::path& shaderPath) {

		std::error_code ec;
		const std::filesystem::path normalized = std::filesystem::weakly_canonical(shaderPath, ec);
		const std::string key =
			Engine::Algorithm::ConvertString((ec ? shaderPath.lexically_normal() : normalized).generic_wstring());
		if (!scannedShaderFiles_.insert(key).second) {
			return;
		}

		std::ifstream file(shaderPath);
		if (!file.is_open()) {
			return;
		}

		static const std::regex kIncludePattern(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
		std::string line;
		while (std::getline(file, line)) {

			std::smatch match;
			if (!std::regex_search(line, match, kIncludePattern) || match.size() < 2) {
				continue;
			}

			const std::filesystem::path includeName = Engine::Algorithm::PathFromUTF8(match[1].str());
			const std::array<std::filesystem::path, 3> candidates = {
				shaderPath.parent_path() / includeName,
				Engine::RuntimePaths::GetEngineAssetPath("Shaders") / includeName,
				Engine::RuntimePaths::GetGameRoot() / "GameAssets/Shaders" / includeName,
			};
			for (const std::filesystem::path& candidate : candidates) {

				if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
					ec.clear();
					continue;
				}
				const std::string assetPath = Engine::RuntimePaths::ToAssetPath(candidate);
				AddLogicalFile(assetPath);
				CollectShaderIncludes(candidate);
				break;
			}
		}
	}

	void GameBuildAssetCollector::CollectModelSidecars(const std::filesystem::path& modelPath) {

		std::vector<std::filesystem::path> dependencies;
		std::string diagnostic;
		const bool loaded = ModelFileDependencyCollector::Collect(modelPath, dependencies, diagnostic);
		// 実際に読んだ文書と解決済み画像を製品へ含める
		for (const auto& path : dependencies) {
			const auto assetPath = RuntimePaths::ToAssetPath(path);
			if (!assetPath.empty()) {
				AddLogicalFile(assetPath);
			}
		}
		if (!loaded) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "モデルの依存ファイルを収集できません path={} 内容={}",
				Algorithm::PathToUTF8(modelPath), diagnostic);
		}
	}
}
