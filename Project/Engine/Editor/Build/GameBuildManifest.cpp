#include "GameBuildManifest.h"
#include "GameBuildUtility.h"
#include "GameBuildAssetCollector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
	std::string ToHex64(uint64_t value) {

		std::ostringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(16) << value;
		return stream.str();
	}
}
using namespace Engine::GameBuildUtility;
using BuildFileEntry = Engine::GameBuildFileEntry;

bool Engine::GameBuildManifest::Write(const GameBuildSettings& settings, const AssetDatabase& database,
	std::filesystem::path& manifestPath, std::filesystem::path& outputDirectory,
	std::filesystem::path& outScriptPath, std::string& outError, SceneAssetStorage* sceneStorage) {

	const AssetMeta* sceneMeta = database.Find(settings.startupScene);
	if (!sceneMeta || sceneMeta->type != AssetType::Scene ||
		!StartsWith(sceneMeta->assetPath, "GameAssets/")) {
		outError = "GameAssets内の最初のシーンを選択してください";
		return false;
	}

	std::string productName;
	if (!ResolveProductName(settings.executableName, productName, outError)) {
		return false;
	}

	std::error_code ec;
	if (settings.outputRoot.empty()) {
		outError = "出力先を選択してください";
		return false;
	}
	std::filesystem::create_directories(settings.outputRoot, ec);
	if (ec || !std::filesystem::is_directory(settings.outputRoot, ec)) {
		outError = "出力先フォルダを作成できません";
		return false;
	}

	const std::filesystem::path gameRoot = RuntimePaths::GetGameRoot();
	const std::filesystem::path buildRoot = ResolveGameBuildRoot(gameRoot);
	const std::filesystem::path projectNamePath = gameRoot.filename();
	const std::string projectName = Algorithm::PathToUTF8(projectNamePath);
	std::filesystem::path projectFileName = projectNamePath;
	projectFileName += L".vcxproj";
	const std::filesystem::path projectPath = gameRoot / projectFileName;
	const std::filesystem::path sourceRuntime =
		buildRoot / "Generated/Output/Release" / projectNamePath;
	outScriptPath = ResolveGameBuildScript(buildRoot);

	if (!std::filesystem::is_regular_file(projectPath, ec)) {
		outError = "ゲームプロジェクトが見つかりません: " + Algorithm::PathToUTF8(projectPath);
		return false;
	}
	if (!std::filesystem::is_regular_file(outScriptPath, ec)) {
		outError = "製品ビルドスクリプトが見つかりません: " + Algorithm::PathToUTF8(outScriptPath);
		return false;
	}

	std::vector<BuildFileEntry> files;
	if (!GameBuildAssetCollector::CollectFiles(settings.startupScene, database, files, outError, sceneStorage)) {
		return false;
	}

	nlohmann::json manifest = nlohmann::json::object();
	manifest["schemaVersion"] = 2;
	manifest["projectPath"] = Algorithm::ConvertString(projectPath.generic_wstring());
	manifest["gameRoot"] = Algorithm::PathToUTF8(gameRoot);
	const std::filesystem::path engineProjectRoot = RuntimePaths::GetEngineProjectRoot();
	if (std::filesystem::is_regular_file(engineProjectRoot / "Include/NEMEngineRuntime.h", ec)) {
		// SDKでは同梱済みツールを使い、エンジンソースをビルドしない
		manifest["buildToolProject"] = "";
		manifest["buildToolExecutable"] = Algorithm::PathToUTF8(
			engineProjectRoot / "Tools/NEMBuildTool/NEMBuildTool.exe");
	} else {
		manifest["buildToolProject"] = Algorithm::PathToUTF8(
			engineProjectRoot / "Tools/NEM.BuildTool/NEMBuildTool.vcxproj");
		manifest["buildToolExecutable"] = Algorithm::PathToUTF8(
			engineProjectRoot.parent_path() /
			"Generated/Output/Release/NEMBuildTool/NEMBuildTool.exe");
	}
	manifest["sourceRuntime"] = Algorithm::ConvertString(sourceRuntime.generic_wstring());
	manifest["runtimeExecutable"] = projectName + ".exe";
	manifest["outputRoot"] = Algorithm::ConvertString(settings.outputRoot.generic_wstring());
	manifest["productName"] = productName;
	manifest["executableName"] = productName + ".exe";
	manifest["projectGuid"] = RuntimePaths::GetProjectGUID();
	manifest["startupScene"] = ToString(settings.startupScene);
	manifest["startupFullscreen"] = settings.startupFullscreen;
	manifest["packages"] = nlohmann::json::array();
	std::vector<ResolvedPackage> packages = RuntimePaths::GetPackages();
	std::sort(packages.begin(), packages.end(),
		[](const ResolvedPackage& lhs, const ResolvedPackage& rhs) {
			return lhs.name < rhs.name;
		});
	for (const ResolvedPackage& package : packages) {
		manifest["packages"].push_back({
			{ "name", package.name },
			{ "version", package.version },
			{ "contentHash", ToHex64(package.contentHash) },
			});
	}

	std::string cookHashSource;
	manifest["files"] = nlohmann::json::array();
	for (const BuildFileEntry& file : files) {
		manifest["files"].push_back({
			{ "source", Algorithm::ConvertString(file.source.generic_wstring()) },
			{ "destination", file.destination },
			{ "size", file.size },
			{ "sha256", file.sha256 },
			});
		cookHashSource += file.destination;
		cookHashSource.push_back('\0');
		cookHashSource += file.sha256;
		cookHashSource.push_back('\0');
		cookHashSource += std::to_string(file.size);
		cookHashSource.push_back('\n');
	}
	const auto* cookHashBytes =
		reinterpret_cast<const uint8_t*>(cookHashSource.data());
	manifest["cookHash"] = ContentHash::SHA256(
		std::span<const uint8_t>(cookHashBytes, cookHashSource.size()));
	if (manifest["cookHash"].get_ref<const std::string&>().empty()) {
		outError = "Cookマニフェストのハッシュを計算できません";
		return false;
	}

	const std::filesystem::path manifestDirectory = buildRoot / "Generated/GameBuild";
	std::filesystem::create_directories(manifestDirectory, ec);
	if (ec) {
		outError = "ビルド用一時フォルダを作成できません";
		return false;
	}
	std::filesystem::path manifestFileName = projectNamePath;
	manifestFileName += L".gameBuildManifest.json";
	manifestPath = manifestDirectory / manifestFileName;
	if (!JsonAdapter::SaveCanonical(manifestPath, manifest)) {
		outError = "ビルド用マニフェストを作成できません";
		return false;
	}

	outputDirectory = settings.outputRoot / Algorithm::PathFromUTF8(productName);
	return true;
}
