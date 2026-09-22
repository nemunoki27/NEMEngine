#include "GameBuildAssetCollector.h"
#include "GameBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <exception>
#include <algorithm>

namespace Engine {

using namespace GameBuildUtility;
using BuildFileEntry = GameBuildFileEntry;

GameBuildAssetCollector::GameBuildAssetCollector(const Engine::AssetDatabase& database, Engine::SceneAssetStorage* sceneStorage) :
			database_(database), sceneStorage_(sceneStorage ? sceneStorage : &standaloneStorage_) {
}

bool GameBuildAssetCollector::Collect(Engine::AssetID startupScene, std::vector<BuildFileEntry>& outFiles, std::string& outError) {

	if (!sceneStorage_->GetRecoveries(true).empty()) {
		outError = "未完了のシーン操作があります、Projectパネルのシーンデータ検証・修復から復旧してください";
		return false;
	}
	AddAllGameAssets();
	AddPackageFiles();
	AddAsset(startupScene);
	for (const Engine::AssetID assetID : Engine::BuiltinAssets::Runtime::Assets) {
		AddAsset(assetID, true);
	}

	AddFixedRuntimeFiles();
	AddDefaultMaterialAssets();
	ProcessAssets();

	if (!errors_.empty()) {
		outError = errors_.front();
		return false;
	}

	outFiles.reserve(files_.size());
	for (const auto& [destination, source] : files_) {

		std::error_code ec;
		const uintmax_t size = std::filesystem::file_size(source, ec);
		const std::string sha256 = ec ? std::string{} :
			Engine::ContentHash::FileSHA256(source);
		if (ec || sha256.empty()) {
			outError = "ビルド対象ファイルのハッシュを計算できません: " +
				Engine::Algorithm::PathToUTF8(source);
			return false;
		}
		outFiles.push_back({ source, destination, size, sha256 });
	}
	return true;
}

void GameBuildAssetCollector::AddAsset(Engine::AssetID assetID, bool required) {

	if (!assetID) return;
	const bool newlyRequired = required && requiredAssets_.insert(assetID).second;
	if (queuedAssets_.insert(assetID).second || newlyRequired) {
		assetQueue_.push_back(assetID);
	}
}

void GameBuildAssetCollector::AddFile(const std::filesystem::path& source, const std::string& destination, bool required) {

	std::error_code ec;
	if (source.empty() || destination.empty() ||
		!std::filesystem::is_regular_file(source, ec) || ec) {
		if (required) errors_.push_back("製品に必要なファイルを読み込めません: " +
			Engine::Algorithm::PathToUTF8(source));
		return;
	}
	if (IsEditorOnlyAsset(destination) || IsGameEditorOnlyAsset(destination)) {
		return;
	}
	const auto [it, inserted] = files_.emplace(destination, source);
	if (!inserted &&
		NormalizeBuildPath(it->second) != NormalizeBuildPath(source)) {
		errors_.push_back("ビルド出力パスが重複しています: " + destination);
	}
}

std::string GameBuildAssetCollector::ToBuildDestination(const std::string& assetPath) const {

	constexpr std::string_view kPackageScheme = "package://";
	if (!assetPath.starts_with(kPackageScheme)) {
		return assetPath;
	}
	return "Packages/" +
		assetPath.substr(kPackageScheme.size());
}

void GameBuildAssetCollector::AddAssetFile(const Engine::AssetMeta& meta) {

	if (meta.type == Engine::AssetType::Script ||
		IsEditorOnlyAsset(meta.assetPath) || IsGameEditorOnlyAsset(meta.assetPath)) {
		return;
	}

	const std::filesystem::path source = database_.ResolveFullPath(meta.guid);
	const std::string destination = ToBuildDestination(meta.assetPath);
	AddFile(source, destination, requiredAssets_.contains(meta.guid));

	std::filesystem::path metaPath = source;
	metaPath += L".meta";
	AddFile(metaPath, destination + ".meta", requiredAssets_.contains(meta.guid));
}

void GameBuildAssetCollector::AddLogicalFile(const std::string& assetPath) {

	if (const Engine::AssetMeta* meta = database_.FindByPath(assetPath)) {
		AddAsset(meta->guid);
		return;
	}
	const std::filesystem::path source = Engine::RuntimePaths::ResolveAssetPath(assetPath);
	AddFile(source, assetPath);
	std::filesystem::path metaPath = source;
	metaPath += L".meta";
	AddFile(metaPath, assetPath + ".meta");
}

void GameBuildAssetCollector::AddAllGameAssets() {

	const std::filesystem::path gameRoot = Engine::RuntimePaths::GetGameRoot();
	const std::filesystem::path gameAssetsRoot = gameRoot / "GameAssets";
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(gameAssetsRoot, ec), end;
		it != end && !ec; it.increment(ec)) {

		if (it->is_directory(ec)) {

			if (Engine::Algorithm::ToLower(
				Engine::Algorithm::PathToUTF8(it->path().filename())) == "externalactors") {
				it.disable_recursion_pending();
			}
			continue;
		}
		if (!it->is_regular_file(ec)) {
			continue;
		}

		const std::filesystem::path relative = std::filesystem::relative(it->path(), gameRoot, ec);
		if (ec) {
			break;
		}
		const std::string assetPath = Engine::Algorithm::ConvertString(relative.generic_wstring());
		AddFile(it->path(), assetPath);

		if (Engine::Algorithm::EndsWith(Engine::Algorithm::ToLower(assetPath), ".meta")) {
			continue;
		}
		if (Engine::Algorithm::EndsWith(
			Engine::Algorithm::ToLower(assetPath), ".actor.json")) {
			const nlohmann::json actor = LoadJson(it->path());
			if (actor.is_object()) {
				InspectJson(actor);
			}
			continue;
		}
		if (const Engine::AssetMeta* meta = database_.FindByPath(assetPath)) {
			AddAsset(meta->guid);
		}
	}
	if (ec) {
		errors_.push_back("GameAssetsのファイルを収集できません: " + ec.message());
	}
}

void GameBuildAssetCollector::AddPackageFiles() {

	for (const Engine::ResolvedPackage& package :
		Engine::RuntimePaths::GetPackages()) {

		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(
			package.root,
			std::filesystem::directory_options::skip_permission_denied, ec);
			it != std::filesystem::recursive_directory_iterator{};
			it.increment(ec)) {

			if (ec) {
				errors_.push_back("Packageを収集できません: " +
					package.name + ": " + ec.message());
				ec.clear();
				break;
			}
			if (!it->is_regular_file(ec)) {
				continue;
			}

			const std::filesystem::path relative =
				it->path().lexically_relative(package.root);
			const std::string destination = "Packages/" +
				package.name + "/" +
				Engine::Algorithm::PathToUTF8(relative);
			AddFile(it->path(), destination);
			if (Engine::Algorithm::EndsWith(
				Engine::Algorithm::ToLower(destination), ".actor.json")) {
				const nlohmann::json actor = LoadJson(it->path());
				if (actor.is_object()) {
					InspectJson(actor);
				}
			}
		}
	}
}

void GameBuildAssetCollector::ProcessAssets() {

	while (!assetQueue_.empty()) {

		const Engine::AssetID assetID = assetQueue_.front();
		assetQueue_.pop_front();

		const Engine::AssetMeta* meta = database_.Find(assetID);
		if (!meta) {
			if (requiredAssets_.contains(assetID)) {
				errors_.push_back("製品に必須のアセットが登録されていません GUID=" + Engine::ToString(assetID));
				continue;
			}
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[ゲームビルド] 参照アセットが見つからないため出力対象から除外します GUID={}",
				Engine::ToString(assetID));
			continue;
		}
		if (meta->type == Engine::AssetType::Script ||
			IsEditorOnlyAsset(meta->assetPath) || IsGameEditorOnlyAsset(meta->assetPath)) {
			continue;
		}

		AddAssetFile(*meta);
		for (const Engine::AssetID dependency : database_.FindDependencies(assetID)) {
			AddAsset(dependency, requiredAssets_.contains(assetID));
		}

		const std::filesystem::path source = database_.ResolveFullPath(assetID);
		InspectFile(*meta, source);
	}
}

void GameBuildAssetCollector::AddFixedRuntimeFiles() {

	AddLogicalFile("Engine/Assets/Config/windowSettings.exeConfig.json");
	for (const Engine::AssetID shaderID : Engine::BuiltinAssets::Shaders::FixedRuntime) {
		AddAsset(shaderID, true);
	}

	for (const char* setting : Engine::ConfigPaths::ProductSettings) {
		const auto source = Engine::RuntimePaths::GetProjectSettingsPath(setting);
		std::error_code ec;
		const bool exists = std::filesystem::exists(source, ec);
		if (ec) {
			errors_.push_back("プロジェクト設定を確認できません: " + Engine::Algorithm::PathToUTF8(source));
			continue;
		}
		// 未設定は実行時の既定値を使い、存在する設定は省略を許さない
		if (!exists) continue;
		const std::string destination = std::string("ProjectSettings/") + setting;
		AddFile(source, destination, true);
	}
}

void GameBuildAssetCollector::AddDefaultMaterialAssets() {

	const Engine::DefaultMaterialSettings& defaults = Engine::DefaultMaterialSettings::GetInstance();
	for (const auto assetID : { defaults.GetMeshOrBuiltin(), defaults.GetSpriteOrBuiltin(),
		defaults.GetTextOrBuiltin(), defaults.GetLineOrBuiltin(), defaults.GetPrimitiveOrBuiltin(),
		defaults.GetPrimitive2DOrBuiltin(), defaults.GetRaytracingReflectionOrBuiltin() }) {
		AddAsset(assetID, true);
	}
}
}

bool Engine::GameBuildAssetCollector::CollectFiles(AssetID startupScene, const AssetDatabase& database,
	std::vector<GameBuildFileEntry>& outFiles, std::string& outError, SceneAssetStorage* sceneStorage) {

	outFiles.clear();
	outError.clear();
	try {
		GameBuildAssetCollector collector(database, sceneStorage);
		return collector.Collect(startupScene, outFiles, outError);
	} catch (const std::exception& exception) {
		// 収集中の例外はビルド失敗として扱い、不完全な一覧を渡さない
		outFiles.clear();
		outError = std::string("製品ファイルの収集中に例外が発生しました: ") + exception.what();
		Logger::Output(LogType::Engine, spdlog::level::err, "[ゲームビルド] {}", outError);
		return false;
	}
}
