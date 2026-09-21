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

//============================================================================
//	AssetDatabase classMethods
//============================================================================
using Engine::AssetFileUtility::IsExternalActorsDirectory;

bool Engine::AssetDatabase::Init() {

	// ファイルパスの初期化
	projectRoot_ = RuntimePaths::GetProjectRoot();
	assetsRoot_ = RuntimePaths::GetEngineAssetsRoot();

	return true;
}

bool Engine::AssetDatabase::RebuildMeta() {

	guidToMeta_.clear();
	pathToGuid_.clear();
	referencersByGuid_.clear();
	issues_.clear();

	const std::filesystem::path gameAssetsRoot = RuntimePaths::GetGameAssetsRoot();
	std::vector<std::filesystem::path> scanRoots{ assetsRoot_ };
	if (gameAssetsRoot != assetsRoot_) {
		scanRoots.emplace_back(gameAssetsRoot);
	}
	for (const ResolvedPackage& package : RuntimePaths::GetPackages()) {
		scanRoots.emplace_back(package.root);
	}

	// 前回規模をヒントに再ハッシュを減らす
	const size_t reserveHint = (std::max<size_t>)(256, guidToMeta_.bucket_count());
	guidToMeta_.reserve(reserveHint);
	pathToGuid_.reserve(reserveHint);
	referencersByGuid_.reserve(reserveHint);

	// 先にUID索引を作り、実体のない.metaを拾ってから依存関係を解決する
	// 依存抽出を索引構築と同時にやると、後から登録される正常アセットをMissing扱いしてしまう
	RebuildIndex(scanRoots);
	DetectOrphanMeta(scanRoots);
	// 依存解決の前に、font.jsonのatlasTextureを隣接アトラスの現在GUIDへ直しておく
	ReconcileFontAtlasReferences();
	RebuildDependencies();

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

		std::error_code ec;
		if (!std::filesystem::exists(scanRoot, ec) || !std::filesystem::is_directory(scanRoot, ec)) {
			continue;
		}

		auto it = std::filesystem::recursive_directory_iterator(
			scanRoot, std::filesystem::directory_options::skip_permission_denied, ec);
		const std::filesystem::recursive_directory_iterator end{};
		if (ec) {
			continue;
		}

		for (; it != end; it.increment(ec)) {

			if (ec) {
				// アクセス不能なものはスキップして走査を継続する
				ec.clear();
				continue;
			}
			if (it->is_directory(ec)) {

				if (IsExternalActorsDirectory(it->path())) {
					it.disable_recursion_pending();
				}
				continue;
			}
			if (!it->is_regular_file(ec)) {
				continue;
			}

			const std::filesystem::path fullPath = it->path();
			const std::string filename = Algorithm::PathToUTF8(fullPath.filename());
			// .metaは索引対象外
			if (Algorithm::EndsWith(filename, ".meta") || filename.find(".meta.") != std::string::npos) {
				continue;
			}

			RegisterAssetFile(fullPath);
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
		if (existing && existing->assetPath != assetPath) {
			AddIssue({ AssetDatabaseIssueType::DuplicatePath, it->second, {},
				AssetType::Unknown, AssetType::Unknown, existing->assetPath, assetPath,
				"path lookup key collision" });
		}
		return it->second;
	}

	const std::filesystem::path assetFull = ResolveAssetPath(assetPath);
	const std::filesystem::path metaFull = MetaPathOf(assetFull);

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

		// typeがUnknownでもパスから一意に判定できるなら補正して保存する
		if (meta.type == AssetType::Unknown && guessedType != AssetType::Unknown) {

			meta.type = guessedType;
			WriteMetaFile(metaFull, meta);
		} else if (meta.type == AssetType::Unknown) {

			AddIssue({ AssetDatabaseIssueType::UnknownAssetType, meta.guid, {},
				AssetType::Unknown, AssetType::Unknown, assetPath, {}, "unresolved asset type" });
		}
	} else {

		// .meta未作成なら新規発行して保存してよい
		meta.guid = AssetGUID::New();
		meta.type = guessedType;
		meta.importer = AssetMetaStorage::ResolveImporterName(guessedType);
		WriteMetaFile(metaFull, meta);
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
	pathToGuid_.emplace(lookupKey, guid);
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

void Engine::AssetDatabase::RefreshDependencies(AssetID id) {

	auto found = guidToMeta_.find(id);
	if (found == guidToMeta_.end()) {
		return;
	}

	// 古い逆引き参照を外してから現在のファイル内容で張り直す
	for (AssetID dependency : found->second.dependencies) {
		auto referencers = referencersByGuid_.find(dependency);
		if (referencers == referencersByGuid_.end()) {
			continue;
		}
		std::erase(referencers->second, id);
		if (referencers->second.empty()) {
			referencersByGuid_.erase(referencers);
		}
	}

	found->second.dependencies = ExtractDependencies(found->second);
	for (AssetID dependency : found->second.dependencies) {
		auto& referencers = referencersByGuid_[dependency];
		if (std::find(referencers.begin(), referencers.end(), id) == referencers.end()) {
			referencers.emplace_back(id);
		}
	}
}

bool Engine::AssetDatabase::UpdateImporterSettings(AssetID id,
	const nlohmann::json& settings, uint32_t importerVersion) {

	auto found = guidToMeta_.find(id);
	if (found == guidToMeta_.end()) {
		return false;
	}

	AssetMeta& meta = found->second;
	const nlohmann::json previousSettings = meta.importerSettings;
	const uint32_t previousVersion = meta.importerVersion;
	meta.importerSettings = settings.is_object() ? settings : nlohmann::json::object();
	meta.importerVersion = importerVersion;

	const std::filesystem::path fullPath = ResolveAssetPath(meta.assetPath);
	if (!WriteMetaFile(MetaPathOf(fullPath), meta)) {

		meta.importerSettings = previousSettings;
		meta.importerVersion = previousVersion;
		return false;
	}
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

void Engine::AssetDatabase::ReconcileFontAtlasReferences() {

	AssetMaintenance::ReconcileFontAtlasReferences(*this);
}

void Engine::AssetDatabase::DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots) {

	AssetMaintenance::DetectOrphanMeta(scanRoots);
}
