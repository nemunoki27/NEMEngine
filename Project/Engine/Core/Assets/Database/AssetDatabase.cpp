#include "AssetDatabase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	AssetDatabase classMethods
//============================================================================
namespace {

	constexpr uint32_t kAssetMetaSchemaVersion = 2;

	bool IsExternalActorsDirectory(const std::filesystem::path& path) {

		return Engine::Algorithm::ToLower(
			Engine::Algorithm::PathToUTF8(path.filename())) == "externalactors";
	}

	std::string_view ResolveImporterName(Engine::AssetType type) {

		switch (type) {
		case Engine::AssetType::Texture:          return "TextureImporter";
		case Engine::AssetType::Mesh:             return "MeshImporter";
		case Engine::AssetType::Audio:            return "AudioImporter";
		case Engine::AssetType::Script:           return "ScriptImporter";
		case Engine::AssetType::Font:             return "FontImporter";
		case Engine::AssetType::Scene:            return "SceneImporter";
		case Engine::AssetType::Prefab:           return "PrefabImporter";
		case Engine::AssetType::Material:         return "MaterialImporter";
		case Engine::AssetType::Shader:           return "ShaderImporter";
		case Engine::AssetType::RenderPipeline:   return "RenderPipelineImporter";
		case Engine::AssetType::AnimationClip:    return "AnimationClipImporter";
		case Engine::AssetType::ParticleEffect:   return "ParticleEffectImporter";
		case Engine::AssetType::ShaderGraph:      return "ShaderGraphImporter";
		case Engine::AssetType::RenderFeatureProfile:
			return "RenderFeatureProfileImporter";
		default:                                  return "DefaultImporter";
		}
	}

	// 例外を投げずにJSONファイルを読み解析失敗時はis_discarded()のjsonを返す
	// 大量のファイルを走査するため、parse_errorの一次例外でデバッガを埋めないようにする
	nlohmann::json LoadJsonFileNoThrow(const std::filesystem::path& path) {

		std::ifstream ifs(path, std::ios::binary);
		if (!ifs.is_open()) {
			return nlohmann::json{};
		}
		const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		return nlohmann::json::parse(content, nullptr, false);
	}

	// 依存抽出で対象にする参照キーと、その期待AssetType
	// stableID/localFileID等のScene内部IDはここに無いため誤検出しない
	const std::unordered_map<std::string, Engine::AssetType>& ReferenceKeyMap() {

		static const std::unordered_map<std::string, Engine::AssetType> kMap = {
			{ "mesh", Engine::AssetType::Mesh },
			{ "material", Engine::AssetType::Material },
			{ "materials", Engine::AssetType::Material },
			{ "materialGuid", Engine::AssetType::Material },
			{ "shaderGraph", Engine::AssetType::ShaderGraph },
			{ "subGraph", Engine::AssetType::ShaderGraph },
			{ "texture", Engine::AssetType::Texture },
			{ "baseColorTexture", Engine::AssetType::Texture },
			{ "normalTexture", Engine::AssetType::Texture },
			{ "metallicRoughnessTexture", Engine::AssetType::Texture },
			{ "metallicTexture", Engine::AssetType::Texture },
			{ "roughnessTexture", Engine::AssetType::Texture },
			{ "displacementTexture", Engine::AssetType::Texture },
			{ "emissiveTexture", Engine::AssetType::Texture },
			{ "occlusionTexture", Engine::AssetType::Texture },
			{ "specularTexture", Engine::AssetType::Texture },
			{ "font", Engine::AssetType::Font },
			{ "atlasTexture", Engine::AssetType::Texture },
			{ "audioClip", Engine::AssetType::Audio },
			{ "script", Engine::AssetType::Script },
			{ "scriptAsset", Engine::AssetType::Script },
			{ "prefab", Engine::AssetType::Prefab },
			{ "prefabAsset", Engine::AssetType::Prefab },
			{ "effect", Engine::AssetType::ParticleEffect },
			{ "shader", Engine::AssetType::Shader },
			{ "shaderOverride", Engine::AssetType::Shader },
			{ "sourceShader", Engine::AssetType::Shader },
			{ "functionFileAsset", Engine::AssetType::Shader },
			{ "file", Engine::AssetType::Shader },
			{ "pipeline", Engine::AssetType::RenderPipeline },
			{ "renderFeatureProfile", Engine::AssetType::RenderFeatureProfile },
			{ "animationClip", Engine::AssetType::AnimationClip },
			{ "scene", Engine::AssetType::Scene },
			{ "activeScene", Engine::AssetType::Scene },
			{ "assetId", Engine::AssetType::Unknown },
			{ "controller", Engine::AssetType::Unknown },
		};
		return kMap;
	}

	// 1つのJSON値を参照候補として登録する(UID形式のときだけ)
	void TryCollectReference(const nlohmann::json& value,
		Engine::AssetType expectedType,
		std::unordered_map<Engine::AssetID, Engine::AssetType>& outIDs,
		std::unordered_map<std::string, Engine::AssetType>& outPaths) {

		if (!value.is_string()) {
			return;
		}
		const std::string reference = value.get<std::string>();
		const std::optional<Engine::AssetID> parsed =
			Engine::TryParseAssetGUID32Hex(reference);
		if (parsed) {
			// 同一IDが複数キーで現れた場合は最初の期待型を維持する
			outIDs.emplace(*parsed, expectedType);
			return;
		}
		if (expectedType != Engine::AssetType::Unknown &&
			!reference.empty()) {

			outPaths.emplace(reference, expectedType);
		}
	}

	// JSONを再帰走査し、既知の参照キー配下のUIDと論理パスを収集する
	void ScanReferences(const nlohmann::json& node,
		std::unordered_map<Engine::AssetID, Engine::AssetType>& outIDs,
		std::unordered_map<std::string, Engine::AssetType>& outPaths) {

		if (node.is_object()) {

			// Material Instanceのrecord形式ではTexture GUIDがvalue配下に保存される
			if (node.contains("id") &&
				node.contains("name") &&
				node.contains("value")) {

				TryCollectReference(
					node["value"],
					Engine::AssetType::Texture,
					outIDs, outPaths);
			}
			// Shader GraphのTexture2D既定値も生成Material作成前から依存として保持する
			if (node.value("type", std::string{}) == "Texture2D" &&
				node.contains("defaultValue")) {

				TryCollectReference(
					node["defaultValue"],
					Engine::AssetType::Texture,
					outIDs, outPaths);
			}

			const auto& keyMap = ReferenceKeyMap();
			for (auto it = node.begin(); it != node.end(); ++it) {

				const auto found = keyMap.find(it.key());
				if (found != keyMap.end()) {

					if (it->is_array()) {
						for (const auto& element : *it) {
							TryCollectReference(element, found->second,
								outIDs, outPaths);
						}
					} else {
						TryCollectReference(*it, found->second,
							outIDs, outPaths);
					}
				}
				// 参照キーでなくても、ネストした参照を拾うため再帰する
				ScanReferences(*it, outIDs, outPaths);
			}
		} else if (node.is_array()) {

			for (const auto& element : node) {
				ScanReferences(element, outIDs, outPaths);
			}
		}
	}
}

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

void Engine::AssetDatabase::ReconcileFontAtlasReferences() {

	// .font.jsonのatlasTextureを隣接する同名アトラス画像の現在GUIDへ揃えて書き戻す
	// フォントを.meta無しでコピーするとGUIDが再採番され参照が切れるため、永続的に直す
	const std::string fontSuffix = ".font.json";
	for (const auto& [guid, meta] : guidToMeta_) {

		if (meta.type != AssetType::Font || !Algorithm::EndsWith(meta.assetPath, fontSuffix)) {
			continue;
		}
		// <name>.font.jsonと同じ場所の<name>.pngをアトラスとする
		const std::string atlasPath = meta.assetPath.substr(0, meta.assetPath.size() - fontSuffix.size()) + ".png";
		const AssetMeta* atlasMeta = FindByPath(atlasPath);
		if (!atlasMeta) {
			continue;
		}
		const std::filesystem::path fullPath = ResolveFullPath(guid);
		nlohmann::json data = LoadJsonFileNoThrow(fullPath);
		if (!data.is_object()) {
			continue;
		}
		// 既に有効なTextureのGUIDを指しているなら尊重して触らない、ここが冪等性も担保する
		if (const std::optional<AssetID> currentGuid = TryParseAssetGUID32Hex(data.value("atlasTexture", std::string{}))) {
			const AssetMeta* current = Find(*currentGuid);
			if (current && current->type == AssetType::Texture) {
				continue;
			}
		}

		// 参照が切れている(パス指定/空/未登録GUID)ので隣接アトラスのGUIDへ直す
		data["atlasTexture"] = ToString(atlasMeta->guid);
		std::ofstream ofs(fullPath, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) {
			continue;
		}
		ofs << data.dump(2);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"[AssetDatabase] Font Atlasを再接続しました Font={} Atlas={}", meta.assetPath, atlasPath);
	}
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

		if (!TryLoadMeta(metaFull, meta)) {

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
			SaveMeta(metaFull, meta);
		} else if (meta.type == AssetType::Unknown) {

			AddIssue({ AssetDatabaseIssueType::UnknownAssetType, meta.guid, {},
				AssetType::Unknown, AssetType::Unknown, assetPath, {}, "unresolved asset type" });
		}
	} else {

		// .meta未作成なら新規発行して保存してよい
		meta.guid = AssetGUID::New();
		meta.type = guessedType;
		meta.importer = ResolveImporterName(guessedType);
		SaveMeta(metaFull, meta);
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
	if (!SaveMeta(MetaPathOf(fullPath), meta)) {

		meta.importerSettings = previousSettings;
		meta.importerVersion = previousVersion;
		return false;
	}
	return true;
}

std::vector<Engine::AssetID> Engine::AssetDatabase::ExtractDependencies(const AssetMeta& meta) {

	std::vector<AssetID> dependencies;

	// JSONベースのアセットだけが内部に参照を持つ
	if (!AssetTypeResolver::IsJsonAssetType(meta.type)) {
		return dependencies;
	}

	const std::filesystem::path fullPath = ResolveAssetPath(meta.assetPath);
	if (fullPath.empty()) {
		return dependencies;
	}
	// Shader種別には.hlsl/.hlsli等の非JSONも含まれるため、実体が.jsonのものだけ解析する
	if (Algorithm::ToLower(Algorithm::PathToUTF8(fullPath.extension())) != ".json") {
		return dependencies;
	}

	const nlohmann::json data = LoadJsonFileNoThrow(fullPath);
	if (!data.is_object() && !data.is_array()) {
		return dependencies;
	}

	// 既知の参照キー配下からGUIDとシェーダー等の論理パスを収集する
	std::unordered_map<AssetID, AssetType> candidates;
	std::unordered_map<std::string, AssetType> pathCandidates;
	ScanReferences(data, candidates, pathCandidates);
	for (const auto& [assetPath, expectedType] : pathCandidates) {

		const AssetMeta* referenced = FindByPath(assetPath);
		if (!referenced) {
			AddIssue({ AssetDatabaseIssueType::MissingReference, meta.guid, {},
				expectedType, AssetType::Unknown, meta.assetPath, assetPath,
				"missing path reference" });
			continue;
		}
		candidates.emplace(referenced->guid, expectedType);
	}

	dependencies.reserve(candidates.size());
	for (const auto& [referencedID, expectedType] : candidates) {

		const AssetMeta* referenced = Find(referencedID);
		if (!referenced) {

			AddIssue({ AssetDatabaseIssueType::MissingReference, meta.guid, referencedID,
				expectedType, AssetType::Unknown, meta.assetPath, {}, "missing reference" });
		} else if (expectedType != AssetType::Unknown && referenced->type != expectedType) {

			AddIssue({ AssetDatabaseIssueType::ReferenceTypeMismatch, meta.guid, referencedID,
				expectedType, referenced->type, meta.assetPath, referenced->assetPath, "type mismatch" });
		}
		dependencies.emplace_back(referencedID);
	}
	return dependencies;
}

void Engine::AssetDatabase::DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots) {

	// 走査中にファイルを消すとiteratorが壊れるので、先に孤立.metaを集めてから削除する
	std::vector<std::filesystem::path> orphanMetas;

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

			const std::filesystem::path metaPath = it->path();
			// "<asset>.meta" のみを対象にする(.meta.バックアップ等は対象外)
			const std::string filename = Algorithm::PathToUTF8(metaPath.filename());
			if (!Algorithm::EndsWith(filename, ".meta") || filename.find(".meta.") != std::string::npos) {
				continue;
			}

			std::filesystem::path assetFull = metaPath;
			assetFull.replace_extension("");
			if (!std::filesystem::exists(assetFull, ec)) {
				orphanMetas.emplace_back(metaPath);
			}
		}
	}

	// 元アセットが消えた孤立.metaは自動削除する、読み取り専用などで消せなければ警告だけ出す
	for (const std::filesystem::path& metaPath : orphanMetas) {

		std::error_code ec;
		if (std::filesystem::remove(metaPath, ec)) {

			Logger::Output(LogType::Engine, "[AssetDatabase] 孤立した.metaを削除しました path={}",
				Algorithm::PathToUTF8(metaPath));
		} else {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[AssetDatabase] 孤立した.metaを削除できません path={}",
				Algorithm::PathToUTF8(metaPath));
		}
	}
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

std::string Engine::AssetDatabase::NormalizeLookupKey(const std::filesystem::path& path) {

	// Windowsの大文字小文字差で別キーにならないよう、正規化+小文字化する
	return Algorithm::ToLower(Algorithm::ConvertString(path.lexically_normal().generic_wstring()));
}

std::filesystem::path Engine::AssetDatabase::MetaPathOf(const std::filesystem::path& assetFullPath) {

	std::filesystem::path metaPath = assetFullPath;
	metaPath += L".meta";
	return metaPath;
}

bool Engine::AssetDatabase::TryLoadMeta(const std::filesystem::path& metaFullPath, AssetMeta& out) const {

	const nlohmann::json data = LoadJsonFileNoThrow(metaFullPath);
	if (!data.is_object() || data.value("schemaVersion", 0u) != kAssetMetaSchemaVersion) {
		return false;
	}

	// guidは厳密にパースし欠落や不正や0はすべて破損扱い
	const std::string guidStr = data.value("guid", "");
	const std::optional<AssetID> parsedGuid = TryParseAssetGUID32Hex(guidStr);
	if (!parsedGuid) {
		return false;
	}
	out.guid = *parsedGuid;

	// 未知typeでも例外にせず、Unknownとして扱う(補正は呼び出し側)
	const std::string typeStr = data.value("type", "Unknown");
	out.type = EnumAdapter<AssetType>::FromString(typeStr).value_or(AssetType::Unknown);
	out.importer = data.value("importer", std::string(ResolveImporterName(out.type)));
	out.importerVersion = data.value("importerVersion", 1u);
	if (const auto it = data.find("settings"); it != data.end() && it->is_object()) {
		out.importerSettings = *it;
	} else {
		out.importerSettings = nlohmann::json::object();
	}

	std::filesystem::path assetFullPath = metaFullPath;
	assetFullPath.replace_extension("");
	out.assetPath = RuntimePaths::ToAssetPath(assetFullPath);
	if (out.assetPath.empty()) {
		return false;
	}
	return true;
}

bool Engine::AssetDatabase::SaveMeta(const std::filesystem::path& metaFullPath, const AssetMeta& meta) const {

	// 既存の .meta を読み、script importer が書く "scripts" 等の未知キーを保持したまま
	// 既知キーだけ更新し、AssetDatabaseがguid採番で書き直してもsidecarの追加情報を壊さない
	nlohmann::json data = LoadJsonFileNoThrow(metaFullPath);
	if (!data.is_object()) {
		data = nlohmann::json::object();
	}

	data["schemaVersion"] = kAssetMetaSchemaVersion;
	data["guid"] = ToString(meta.guid);
	data["type"] = std::string(EnumAdapter<AssetType>::ToString(meta.type));
	data["importer"] = meta.importer.empty() ?
		std::string(ResolveImporterName(meta.type)) : meta.importer;
	data["importerVersion"] = meta.importerVersion;
	data["settings"] = meta.importerSettings.is_object() ?
		meta.importerSettings : nlohmann::json::object();
	data.erase("version");

	std::ofstream ofs(metaFullPath, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open()) {
		return false;
	}

	// ファイルに書き込む
	ofs << data.dump(2);

	return true;
}
