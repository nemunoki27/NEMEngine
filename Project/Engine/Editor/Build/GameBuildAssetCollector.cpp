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
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <exception>
#include <algorithm>
#include <tuple>
#include <unordered_map>

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
	AddAdditionalAssets();
	reason_ = "最初のシーン";
	AddAsset(startupScene, true);
	reason_ = "エンジンの既定アセット";
	for (const Engine::AssetID assetID : Engine::BuiltinAssets::Runtime::Assets) {
		AddAsset(assetID, true);
	}

	for (const auto& package : RuntimePaths::GetPackages()) {
		AddFile(package.root / "package.json", "Packages/" + package.name + "/package.json", true);
	}
	AddFixedRuntimeFiles();
	AddDefaultMaterialAssets();
	ProcessAssets();
	reason_ = "Shader Graphのコンパイル";
	if (needsGraphShaderSources_) {
		// GraphがCook時に生成するHLSLから参照される組み込みソースも作業先に配置する
		const auto root = RuntimePaths::GetEngineAssetPath("Rendering/Builtin");
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			const auto& path = entry.path();
			if (path.extension() != ".hlsli" && path.extension() != ".hlsl") continue;
			const auto destination = RuntimePaths::ToAssetPath(path);
			AddFile(path, destination, true);
			auto meta = path;
			meta += L".meta";
			if (std::filesystem::is_regular_file(meta)) AddFile(meta, destination + ".meta", true);
		}
	}
	for (const auto& [destination, source] : files_) CollectNotices(source.parent_path());
	// 同じ欠損集合の確認結果を再収集時にも照合できる順序にする
	std::sort(warnings_.begin(), warnings_.end(), [](const GameBuildWarning& lhs, const GameBuildWarning& rhs) {
		return std::tie(lhs.assetPath, lhs.detail, lhs.assetID, lhs.referenceID) <
			std::tie(rhs.assetPath, rhs.detail, rhs.assetID, rhs.referenceID);
	});

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
		outFiles.push_back({ source, destination, size, sha256, fileReasons_.at(destination) });
	}
	return true;
}

void GameBuildAssetCollector::AddWarning(AssetID asset, AssetID reference, std::string path, std::string detail) {

	GameBuildWarning warning{ asset, reference, std::move(path), std::move(detail) };
	if (std::find(warnings_.begin(), warnings_.end(), warning) == warnings_.end()) {
		warnings_.push_back(std::move(warning));
	}
}

std::string GameBuildAssetCollector::DescribeReference(const nlohmann::json& data, const std::string& target,
	AssetType type, std::string detail) {

	if (type != AssetType::Unknown) detail += std::string("（必要な種類: ") + EnumAdapter<AssetType>::ToString(type) + "）";
	const auto fields = data.flatten();
	for (const auto& [field, value] : fields.items()) {
		if (value.is_string() && value.get_ref<const std::string&>() == target) {
			detail += "\n項目: " + field;
		}
	}
	return detail + "\n参照先: " + target;
}

void GameBuildAssetCollector::AddAsset(Engine::AssetID assetID, bool required) {

	if (!assetID) return;
	const bool newlyRequired = required && requiredAssets_.insert(assetID).second;
	if (queuedAssets_.insert(assetID).second || newlyRequired) {
		assetReasons_.try_emplace(assetID, reason_);
		assetQueue_.push_back(assetID);
	}
}

void GameBuildAssetCollector::AddFile(const std::filesystem::path& source, const std::string& destination, bool required) {

	std::error_code ec;
	if (source.empty() || destination.empty() ||
		!std::filesystem::is_regular_file(source, ec) || ec) {
		if (required) errors_.push_back("製品に必要なファイルを読み込めません: " +
			Engine::Algorithm::PathToUTF8(source));
		else AddWarning(currentAsset_, {}, destination, "参照ファイルが見つかりません");
		return;
	}
	if (IsEditorOnlyAsset(destination) || IsGameEditorOnlyAsset(destination)) {
		return;
	}
	const auto [it, inserted] = files_.emplace(destination, source);
	if (inserted) fileReasons_.emplace(destination, reason_);
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
	std::string destination = "Packages/" + assetPath.substr(kPackageScheme.size());
	std::ranges::replace(destination, '\\', '/');
	return destination;
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
	AddFile(source, ToBuildDestination(assetPath));
	std::filesystem::path metaPath = source;
	metaPath += L".meta";
	if (std::filesystem::is_regular_file(metaPath)) AddFile(metaPath, ToBuildDestination(assetPath) + ".meta");
}

void GameBuildAssetCollector::AddAdditionalAssets() {

	for (const auto& path : LoadAdditionalAssets()) {
		const auto source = NormalizeBuildPath(RuntimePaths::ResolveAssetPath(path));
		const auto actual = RuntimePaths::ToAssetPath(source);
		if (!IsAdditionalAssetPath(path) || actual.empty() || !IsAdditionalAssetPath(actual) || !std::filesystem::exists(source)) {
			errors_.push_back("追加で含めるアセットが見つかりません: " + path);
			continue;
		}
		reason_ = "追加指定: " + path;
		const auto add = [&](const std::filesystem::path& candidate) {
			const auto logical = RuntimePaths::ToAssetPath(NormalizeBuildPath(candidate));
			if (!IsAdditionalAssetPath(logical)) {
				errors_.push_back("追加対象がアセットフォルダー外を参照しています: " + Algorithm::PathToUTF8(candidate));
				return;
			}
			if (!logical.ends_with(".meta")) AddLogicalFile(logical);
		};
		if (std::filesystem::is_regular_file(source)) {
			add(source);
			continue;
		}
		for (std::filesystem::recursive_directory_iterator it(source), end; it != end; ++it) {
			if (it->is_directory() && Algorithm::ToLower(Algorithm::PathToUTF8(it->path().filename())) == "externalactors") {
				it.disable_recursion_pending();
			} else if (it->is_regular_file()) {
				add(it->path());
			}
		}
	}
}

void GameBuildAssetCollector::CollectNotices(const std::filesystem::path& directory) {

	for (auto path = directory; !path.empty() && noticeDirectories_.insert(path).second; path = path.parent_path()) {
		const auto logical = RuntimePaths::ToAssetPath(path);
		if (!IsAdditionalAssetPath(logical) && logical != "Engine/Assets" && !logical.starts_with("Engine/Assets/")) break;
		for (const auto& entry : std::filesystem::directory_iterator(path)) {
			if (!entry.is_regular_file()) continue;
			const auto name = Algorithm::ToLower(Algorithm::PathToUTF8(entry.path().filename()));
			const auto stem = name.substr(0, name.find_first_of(".-"));
			if (!name.ends_with(".meta") && (stem == "license" || stem == "licence" || stem == "copying" ||
				stem == "notice" || stem == "thirdpartynotices" || stem == "attribution" || stem == "ofl")) {
				reason_ = logical + " の配布表記";
				AddFile(entry.path(), ToBuildDestination(RuntimePaths::ToAssetPath(entry.path())), true);
			}
		}
	}
}

void GameBuildAssetCollector::ProcessAssets() {

	// 参照診断をGUIDごとにまとめて重複走査を省く
	std::unordered_map<AssetID, std::vector<const AssetDatabaseIssue*>> issues;
	for (const auto& issue : database_.GetIssues()) {
		if (issue.type == AssetDatabaseIssueType::MissingReference ||
			issue.type == AssetDatabaseIssueType::ReferenceTypeMismatch) {
			issues[issue.assetID].push_back(&issue);
		}
	}
	while (!assetQueue_.empty()) {

		const Engine::AssetID assetID = assetQueue_.front();
		currentAsset_ = assetID;
		assetQueue_.pop_front();

		const Engine::AssetMeta* meta = database_.Find(assetID);
		if (!meta) {
			if (requiredAssets_.contains(assetID)) {
				errors_.push_back("製品に必須のアセットが登録されていません GUID=" + Engine::ToString(assetID));
				continue;
			}
			continue;
		}
		if (meta->type == Engine::AssetType::Script ||
			IsEditorOnlyAsset(meta->assetPath) || IsGameEditorOnlyAsset(meta->assetPath)) {
			continue;
		}

		reason_ = assetReasons_.at(assetID);
		AddAssetFile(*meta);
		reason_ = meta->assetPath;
		if (const auto found = issues.find(assetID); found != issues.end()) {
			const auto data = LoadJson(database_.ResolveFullPath(assetID));
			for (const auto* issue : found->second) {
				const auto target = issue->referencedAssetID ? ToString(issue->referencedAssetID) : issue->relatedPath;
				std::string detail = DescribeReference(data, target, issue->expectedType,
					issue->type == AssetDatabaseIssueType::ReferenceTypeMismatch ? "参照先の種類が一致しません" : "参照先が見つかりません");
				if (issue->type == AssetDatabaseIssueType::ReferenceTypeMismatch) {
					detail += "\n実際の種類: " + std::string(EnumAdapter<AssetType>::ToString(issue->actualType));
					if (!issue->relatedPath.empty()) detail += "\n参照先ファイル: " + issue->relatedPath;
				}
				if (requiredAssets_.contains(assetID) && meta->type != AssetType::Scene) {
					errors_.push_back(meta->assetPath + "\n" + detail);
				} else {
					AddWarning(assetID, issue->referencedAssetID, meta->assetPath, detail);
				}
			}
		}
		for (const Engine::AssetID dependency : database_.FindDependencies(assetID)) {
			AddAsset(dependency, requiredAssets_.contains(assetID) && meta->type != AssetType::Scene);
		}

		const std::filesystem::path source = database_.ResolveFullPath(assetID);
		InspectFile(*meta, source);
	}
}

void GameBuildAssetCollector::AddFixedRuntimeFiles() {

	AddLogicalFile("Engine/Assets/Config/windowSettings.exeConfig.json");
	// 面光源テーブルの配布条件と取り込み元を同梱する
	AddLogicalFile("Engine/Assets/Textures/Lighting/LTC/LICENSE.txt");
	AddLogicalFile("Engine/Assets/Textures/Lighting/LTC/SOURCE.json");
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
		reason_ = destination;
		InspectJson(LoadJson(source), destination, false);
		reason_ = "エンジンの既定アセット";
	}
}

void GameBuildAssetCollector::AddDefaultMaterialAssets() {

	const Engine::DefaultMaterialSettings& defaults = Engine::DefaultMaterialSettings::GetInstance();
	for (const auto assetID : { defaults.GetMeshOrBuiltin(), defaults.GetSpriteOrBuiltin(),
		defaults.GetTextOrBuiltin(), defaults.GetLineOrBuiltin(), defaults.GetPrimitiveOrBuiltin(),
		defaults.GetPrimitive2DOrBuiltin(), defaults.GetDecalOrBuiltin(), defaults.GetRaytracingReflectionOrBuiltin() }) {
		AddAsset(assetID, true);
	}
}
}

bool Engine::GameBuildAssetCollector::CollectFiles(AssetID startupScene, const AssetDatabase& database,
	std::vector<GameBuildFileEntry>& outFiles, std::string& outError, SceneAssetStorage* sceneStorage,
	std::vector<GameBuildWarning>* warnings) {

	outFiles.clear();
	outError.clear();
	if (warnings) { warnings->clear(); }
	try {
		GameBuildAssetCollector collector(database, sceneStorage);
		bool collected = collector.Collect(startupScene, outFiles, outError);
		if (warnings) { *warnings = collector.GetWarnings(); }
		return collected;
	} catch (const std::exception& exception) {
		// 収集中の例外はビルド失敗として扱い、不完全な一覧を渡さない
		outFiles.clear();
		outError = std::string("製品ファイルの収集中に例外が発生しました: ") + exception.what();
		Logger::Output(LogType::Engine, spdlog::level::err, "[ゲームビルド] {}", outError);
		return false;
	}
}
