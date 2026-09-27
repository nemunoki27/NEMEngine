#include "AssetDatabase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Assets/Database/AssetDependencyResolver.h>
#include <Engine/Core/Assets/Database/AssetMaintenance.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <optional>
#include <system_error>
#include <unordered_set>
#include <stdexcept>

//============================================================================
//	AssetDatabase classMethods
//============================================================================
using Engine::AssetFileUtility::IsExternalActorsDirectory;

Engine::AssetDatabase::CacheLifetime::CacheLifetime(const CacheLifetime&) {

	// コピーした索引には別の派生データを持たせる
}

Engine::AssetDatabase::CacheLifetime& Engine::AssetDatabase::CacheLifetime::operator=(const CacheLifetime&) {

	identity = std::make_shared<const uint8_t>(0);
	return *this;
}

bool Engine::AssetDatabase::Init() {

	// 再初期化前の派生データを失効させる
	cacheLifetime_.identity = std::make_shared<const uint8_t>(0);
	// ファイルパスの初期化
	projectRoot_ = RuntimePaths::GetProjectRoot();
	assetsRoot_ = RuntimePaths::GetEngineAssetsRoot();

	return true;
}

bool Engine::AssetDatabase::RebuildMeta() {

	const std::filesystem::path gameAssetsRoot = RuntimePaths::GetGameAssetsRoot();
	std::vector<std::filesystem::path> scanRoots{ assetsRoot_ };
	if (gameAssetsRoot != assetsRoot_) {
		scanRoots.emplace_back(gameAssetsRoot);
	}
	for (const ResolvedPackage& package : RuntimePaths::GetPackages()) {
		scanRoots.emplace_back(package.root);
	}
	return RebuildMeta(scanRoots);
}

bool Engine::AssetDatabase::RebuildMeta(const std::vector<std::filesystem::path>& scanRoots) {

	// 候補の走査と依存解決が完了するまで旧索引を保持する
	AssetDatabase candidate;
	candidate.projectRoot_ = projectRoot_;
	candidate.assetsRoot_ = assetsRoot_;
	try {
		candidate.scanRoots_ = scanRoots;
		candidate.buildingIndex_ = true;
		candidate.guidToMeta_.reserve((std::max<size_t>)(256, guidToMeta_.size()));
		candidate.RebuildIndex(scanRoots);
		candidate.DetectOrphanMeta(scanRoots);
		candidate.RebuildDependencies();
		AssetMaintenance::DetectFontAtlasReferences(candidate, candidate.issues_);
	} catch (const std::exception& error) {
		lastRebuildError_ = error.what();
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[AssetDatabase] 索引の更新に失敗しました 旧索引を保持します 詳細={}", lastRebuildError_);
		return false;
	}
	guidToMeta_.swap(candidate.guidToMeta_);
	pathToGuid_.swap(candidate.pathToGuid_);
	referencersByGuid_.swap(candidate.referencersByGuid_);
	issues_.swap(candidate.issues_);
	scanRoots_.swap(candidate.scanRoots_);
	lastRebuildError_.clear();

	// 診断のサマリをまとめて出力する(詳細は先頭数件のみ)
	size_t duplicateCount = 0;
	size_t orphanCount = 0;
	size_t missingCount = 0;
	for (const AssetDatabaseIssue& issue : issues_) {
		switch (issue.type) {
		case AssetDatabaseIssueType::DuplicateGuid:
		case AssetDatabaseIssueType::DuplicatePath:
			++duplicateCount;
			break;
		case AssetDatabaseIssueType::OrphanMeta:
			++orphanCount;
			break;
		case AssetDatabaseIssueType::MissingReference:
			++missingCount;
			break;
		default:
			break;
		}
	}
	Logger::Output(LogType::Engine,
		"[AssetDatabase] 再構築が完了しました Asset数={} 問題数={} GUID重複={} 孤立Meta={} 参照欠損={}",
		guidToMeta_.size(), issues_.size(), duplicateCount, orphanCount, missingCount);

	constexpr size_t kMaxDetailLog = 16;
	const size_t detailCount = (std::min)(kMaxDetailLog, issues_.size());
	for (size_t i = 0; i < detailCount; ++i) {

		const AssetDatabaseIssue& issue = issues_[i];
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[AssetDatabase] 問題を検出しました 種別={} Asset={} 参照={} path={} 詳細={}",
			static_cast<int>(issue.type), ToString(issue.assetID), ToString(issue.referencedAssetID),
			issue.assetPath, issue.detail);
	}

	// アセット集合が変わったことを外部へ知らせる、ProjectPanel等がこのリビジョン差分で再構築を判断する
	++structureRevision_;
	return true;
}

void Engine::AssetDatabase::RebuildIndex(const std::vector<std::filesystem::path>& scanRoots) {

	for (const std::filesystem::path& scanRoot : scanRoots) {

		if (!std::filesystem::is_directory(scanRoot)) {
			throw std::runtime_error("Asset directory is unavailable: " + Algorithm::PathToUTF8(scanRoot));
		}

		auto it = std::filesystem::recursive_directory_iterator(scanRoot);
		const std::filesystem::recursive_directory_iterator end{};

		for (; it != end; ++it) {
			if (it->is_directory()) {

				if (IsExternalActorsDirectory(it->path())) {
					it.disable_recursion_pending();
				}
				continue;
			}
			if (!it->is_regular_file()) {
				continue;
			}

			const std::filesystem::path fullPath = it->path();
			const std::string filename = Algorithm::PathToUTF8(fullPath.filename());
			// .metaは索引対象外
			if (Algorithm::EndsWith(filename, ".meta") || filename.find(".meta.") != std::string::npos) {
				continue;
			}

			if (!RegisterAssetFile(fullPath)) {
				throw std::runtime_error("Asset registration failed: " + Algorithm::PathToUTF8(fullPath));
			}
		}
	}
}

Engine::AssetID Engine::AssetDatabase::RegisterAssetFile(const std::filesystem::path& assetFullPath) {

	const std::string assetPath = RuntimePaths::ToAssetPath(assetFullPath);
	if (assetPath.empty()) {
		return {};
	}
	const AssetType guessed = AssetTypeResolver::GuessByPath(assetFullPath);
	return ImportOrGet(assetPath, guessed);
}

Engine::AssetID Engine::AssetDatabase::ImportOrGet(const std::string& assetPath, AssetType guessedType) {

	const std::string lookupKey = NormalizeLookupKey(assetPath);

	// 既に索引にあるなら返し、別表記の同一キー衝突はDuplicatePathとして検出する
	if (auto it = pathToGuid_.find(lookupKey); it != pathToGuid_.end()) {

		const AssetMeta* existing = Find(it->second);
		if (existing && guessedType != AssetType::Unknown && guessedType != AssetType::DefaultAsset &&
			existing->type != guessedType) {
			return {};
		}
		if (existing && existing->assetPath != assetPath) {
			AddIssue({ AssetDatabaseIssueType::DuplicatePath, it->second, {},
				AssetType::Unknown, AssetType::Unknown, existing->assetPath, assetPath,
				"path lookup key collision" });
		}
		return it->second;
	}

	const std::filesystem::path assetFull = ResolveAssetPath(assetPath);
	const std::filesystem::path metaFull = MetaPathOf(assetFull);
	if (!std::filesystem::is_regular_file(assetFull)) {
		return {};
	}

	AssetMeta meta{};
	meta.assetPath = assetPath;

	if (std::filesystem::exists(metaFull)) {

		if (!ReadMetaFile(metaFull, meta)) {

			// 壊れた.metaは静かに新UIDで上書きせず診断に残してスキップする
			AddIssue({ AssetDatabaseIssueType::CorruptMeta, {}, {},
				AssetType::Unknown, AssetType::Unknown, assetPath, Algorithm::PathToUTF8(metaFull),
				"failed to parse .meta" });
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[AssetDatabase] 壊れた.metaを無視しました: {}", assetPath);
			return {};
		}

		// 論理パスは現在の走査結果で最新化する
		meta.assetPath = assetPath;
		if (meta.type != AssetType::Unknown && guessedType != AssetType::Unknown &&
			guessedType != AssetType::DefaultAsset && meta.type != guessedType) {

			AddIssue({ AssetDatabaseIssueType::CorruptMeta, meta.guid, {}, guessedType, meta.type,
				assetPath, Algorithm::PathToUTF8(metaFull), "asset type mismatch" });
			return {};
		}

		// typeがUnknownでもパスから一意に判定できるなら補正して保存する
		if (meta.type == AssetType::Unknown && guessedType != AssetType::Unknown) {

			meta.type = guessedType;
			if (!WriteMetaFile(metaFull, meta)) {
				return {};
			}
		} else if (meta.type == AssetType::Unknown) {

			AddIssue({ AssetDatabaseIssueType::UnknownAssetType, meta.guid, {},
				AssetType::Unknown, AssetType::Unknown, assetPath, {}, "unresolved asset type" });
		}
	} else {

		// .meta未作成なら新規発行して保存してよい
		meta.guid = AssetGUID::New();
		meta.type = guessedType;
		meta.importer = AssetMetaStorage::ResolveImporterName(guessedType);
		if (!WriteMetaFile(metaFull, meta)) {
			return {};
		}
	}

	if (!meta.guid) {
		AddIssue({ AssetDatabaseIssueType::CorruptMeta, {}, {},
			AssetType::Unknown, AssetType::Unknown, assetPath, Algorithm::PathToUTF8(metaFull),
			"invalid guid" });
		return {};
	}

	// 重複UIDは後勝ち上書きせず、両方のパスを診断に残す
	if (auto existing = guidToMeta_.find(meta.guid);
		existing != guidToMeta_.end() && existing->second.assetPath != meta.assetPath) {

		AddIssue({ AssetDatabaseIssueType::DuplicateGuid, meta.guid, {},
			AssetType::Unknown, AssetType::Unknown, existing->second.assetPath, meta.assetPath,
			"duplicate guid" });
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[AssetDatabase] GUIDが重複しています {} : '{}' と '{}'",
			ToString(meta.guid), existing->second.assetPath, meta.assetPath);
		return {};
	}

	const AssetID guid = meta.guid;
	guidToMeta_.emplace(guid, std::move(meta));
	try {
		pathToGuid_.emplace(lookupKey, guid);
		if (!buildingIndex_) {
			if (!RefreshDependencies(guid)) {
				pathToGuid_.erase(lookupKey);
				guidToMeta_.erase(guid);
				return {};
			}
			++structureRevision_;
		}
	} catch (...) {
		pathToGuid_.erase(lookupKey);
		guidToMeta_.erase(guid);
		throw;
	}
	return guid;
}

void Engine::AssetDatabase::RebuildDependencies() {

	// 先に全アセットの依存先を確定させる(走査順で未登録扱いにしないため)
	for (auto& [id, meta] : guidToMeta_) {
		meta.dependencies = ExtractDependencies(meta);
	}

	// 依存先が出そろってから逆引きを張る
	for (const auto& [id, meta] : guidToMeta_) {
		for (const AssetID dependency : meta.dependencies) {
			referencersByGuid_[dependency].emplace_back(id);
		}
	}
}

bool Engine::AssetDatabase::RefreshDependencies(AssetID id) {

	auto found = guidToMeta_.find(id);
	if (found == guidToMeta_.end()) {
		return false;
	}

	try {
		// 新しい依存集合を完成させてから逆引きと同時に公開する
		std::vector<AssetDatabaseIssue> nextIssues;
		auto dependencies = AssetDependencyResolver::ExtractDependencies(*this, found->second, nextIssues);
		auto referencerMap = referencersByGuid_;
		for (AssetID dependency : found->second.dependencies) {
			auto referencers = referencerMap.find(dependency);
			if (referencers == referencerMap.end()) {
				continue;
			}
			std::erase(referencers->second, id);
			if (referencers->second.empty()) {
				referencerMap.erase(referencers);
			}
		}

		for (AssetID dependency : dependencies) {
			auto& referencers = referencerMap[dependency];
			if (std::find(referencers.begin(), referencers.end(), id) == referencers.end()) {
				referencers.emplace_back(id);
			}
		}
		issues_.reserve(issues_.size() + nextIssues.size());
		std::erase_if(issues_, [id](const AssetDatabaseIssue& issue) {
			return issue.assetID == id && (issue.type == AssetDatabaseIssueType::MissingReference ||
				issue.type == AssetDatabaseIssueType::ReferenceTypeMismatch);
		});
		for (auto& issue : nextIssues) {
			issues_.emplace_back(std::move(issue));
		}
		found->second.dependencies.swap(dependencies);
		referencersByGuid_.swap(referencerMap);
		return true;
	} catch (const std::exception& error) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[AssetDatabase] 依存の更新に失敗しました 旧参照を保持します ID={} 詳細={}", ToString(id), error.what());
		return false;
	}
}

void Engine::AssetDatabase::NotifyContentChanged(AssetID id) {

	if (guidToMeta_.contains(id)) {
		++contentRevisions_[id];
	}
}

uint64_t Engine::AssetDatabase::GetContentRevision(AssetID id) const {

	const auto found = contentRevisions_.find(id);
	return found == contentRevisions_.end() ? 0 : found->second;
}

bool Engine::AssetDatabase::UpdateImporterSettings(AssetID id,
	const nlohmann::json& settings, uint32_t importerVersion) {

	if (!settings.is_object() || importerVersion == 0) {
		return false;
	}
	auto found = guidToMeta_.find(id);
	if (found == guidToMeta_.end()) {
		return false;
	}

	// 保存する候補を作り、成功するまで公開値を変更しない
	AssetMeta candidate = found->second;
	candidate.importerSettings = settings;
	candidate.importerVersion = importerVersion;

	const std::filesystem::path fullPath = ResolveAssetPath(candidate.assetPath);
	if (!WriteMetaFile(MetaPathOf(fullPath), candidate)) {
		return false;
	}
	found->second.importerSettings.swap(candidate.importerSettings);
	found->second.importerVersion = importerVersion;
	++structureRevision_;
	return true;
}

void Engine::AssetDatabase::AddIssue(AssetDatabaseIssue&& issue) {

	issues_.emplace_back(std::move(issue));
}

const Engine::AssetMeta* Engine::AssetDatabase::Find(AssetID id) const {

	auto it = guidToMeta_.find(id);
	return (it == guidToMeta_.end()) ? nullptr : &it->second;
}

const Engine::AssetMeta* Engine::AssetDatabase::FindByPath(const std::string& assetPath) const {

	auto it = pathToGuid_.find(NormalizeLookupKey(assetPath));
	return (it == pathToGuid_.end()) ? nullptr : Find(it->second);
}

std::filesystem::path Engine::AssetDatabase::ResolveFullPath(AssetID id) const {

	const auto* meta = Find(id);
	if (!meta) {
		return {};
	}
	return ResolveAssetPath(meta->assetPath);
}

std::filesystem::path Engine::AssetDatabase::ResolveAssetPath(const std::string& assetPath) const {

	return RuntimePaths::ResolveAssetPath(assetPath);
}

const std::vector<Engine::AssetID>& Engine::AssetDatabase::FindDependencies(AssetID id) const {

	static const std::vector<AssetID> kEmpty;
	auto it = guidToMeta_.find(id);
	return (it == guidToMeta_.end()) ? kEmpty : it->second.dependencies;
}

const std::vector<Engine::AssetID>& Engine::AssetDatabase::FindReferencers(AssetID id) const {

	static const std::vector<AssetID> kEmpty;
	auto it = referencersByGuid_.find(id);
	return (it == referencersByGuid_.end()) ? kEmpty : it->second;
}

std::vector<Engine::AssetID>
Engine::AssetDatabase::FindReferencersRecursive(AssetID id) const {

	std::vector<AssetID> result;
	std::vector<AssetID> pending{ id };
	std::unordered_set<AssetID> visited{ id };
	for (size_t index = 0; index < pending.size(); ++index) {

		for (AssetID referencer : FindReferencers(pending[index])) {
			if (!visited.insert(referencer).second) {
				continue;
			}
			result.emplace_back(referencer);
			pending.emplace_back(referencer);
		}
	}
	return result;
}

bool Engine::AssetDatabase::HasReferencers(AssetID id) const {

	auto it = referencersByGuid_.find(id);
	return it != referencersByGuid_.end() && !it->second.empty();
}

std::string Engine::AssetDatabase::NormalizeLookupKey(const std::string& assetPath) {

	// 論理パスはUTF-8なのでWindowsの既定コードページを経由させない
	const std::filesystem::path path = Algorithm::PathFromUTF8(assetPath);
	// Windowsの大文字小文字差で別キーにならないよう、正規化+小文字化する
	return Algorithm::ToLower(Algorithm::ConvertString(path.lexically_normal().generic_wstring()));
}

std::filesystem::path Engine::AssetDatabase::MetaPathOf(const std::filesystem::path& assetFullPath) {

	return AssetMetaStorage::MetaPathOf(assetFullPath);
}

bool Engine::AssetDatabase::ReadMetaFile(const std::filesystem::path& metaFullPath, AssetMeta& out) {

	return AssetMetaStorage::ReadMetaFile(metaFullPath, out);
}

bool Engine::AssetDatabase::WriteMetaFile(const std::filesystem::path& metaFullPath, const AssetMeta& meta) {

	return AssetMetaStorage::WriteMetaFile(metaFullPath, meta);
}

std::vector<Engine::AssetID> Engine::AssetDatabase::ExtractDependencies(const AssetMeta& meta) {

	return AssetDependencyResolver::ExtractDependencies(*this, meta, issues_);
}

void Engine::AssetDatabase::DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots) {

	AssetMaintenance::DetectOrphanMeta(scanRoots, issues_);
}
