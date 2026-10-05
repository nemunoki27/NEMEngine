#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Watch/AssetChangeWatcher.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <fstream>
#include <chrono>
#include <thread>

namespace NEMTests {

	bool TestAssetWatcherLifetime() {

		TestDirectory directory("AssetWatcher");
		Engine::AssetChangeWatcher watcher;
		// 通知待ちと書込直後のどちらでも停止・再開始できる
		for (uint32_t index = 0; index < 32; ++index) {
			if (!watcher.Start(directory.GetPath())) {
				return false;
			}
			std::ofstream(directory.GetPath() / "changed.txt") << index;
			watcher.Stop();
			watcher.Stop();
			std::vector<std::filesystem::path> changes;
			watcher.DrainChanges(changes);
			if (watcher.IsRunning() || !changes.empty()) {
				return false;
			}
		}
		if (!watcher.Start(directory.GetPath())) {
			return false;
		}
		// 再開始後も新しい変更を受け取れる
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		std::vector<std::filesystem::path> changes;
		while (changes.empty() && std::chrono::steady_clock::now() < deadline) {
			std::ofstream(directory.GetPath() / "changed.txt") << "restarted";
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			watcher.DrainChanges(changes);
		}
		watcher.Stop();
		return !changes.empty();
	}

	bool TestAssetDatabaseTransactions() {

		using namespace Engine;
		TestDirectory directory("AssetDatabase", RuntimePaths::GetGameAssetsRoot());
		const auto root = directory.GetPath();
		const auto atlasPath = root / "test.png";
		const auto fontPath = root / "test.font.json";
		const auto orphanPath = root / "orphan.png.meta";
		const auto staging = root / ".nem-copy-0123456789abcdef0123456789abcdef";
		std::filesystem::create_directory(staging);
		std::ofstream(staging / "unpublished.png") << "staged";
		std::ofstream(staging / "orphan.png.meta") << "{}";
		if (!AssetFileUtility::IsAssetCopyStagingDirectory(staging) ||
			AssetFileUtility::IsAssetCopyStagingDirectory(root / ".nem-copy-user") ||
			AssetFileUtility::IsAssetCopyStagingDirectory(root / ".nem-copy-0123456789abcdef0123456789abcdeg")) {
			return false;
		}
		std::ofstream(atlasPath) << "fixture";
		if (!JsonFile::Save(fontPath, nlohmann::json{ { "atlasTexture", "" }, { "custom", 17 } }) ||
			!JsonFile::Save(orphanPath, nlohmann::json{ { "unknown", 23 } })) {
			return false;
		}
		AssetDatabase database;
		database.Init();
		if (!database.RebuildMeta({ root })) {
			return false;
		}
		const auto* font = database.FindByPath(RuntimePaths::ToAssetPath(fontPath));
		const auto* atlas = database.FindByPath(RuntimePaths::ToAssetPath(atlasPath));
		if (!font || !atlas) {
			return false;
		}
		const AssetID fontID = font->guid;
		const AssetID atlasID = atlas->guid;
		// 作業先を索引へ登録せず、metaも生成しない
		if (database.FindByPath(RuntimePaths::ToAssetPath(staging / "unpublished.png")) ||
			std::filesystem::exists(staging / "unpublished.png.meta")) {
			return false;
		}
		const auto revision = database.GetStructureRevision();
		nlohmann::json data;
		// 走査は孤立metaと欠損Fontを診断するだけに留める
		bool passed = std::filesystem::exists(orphanPath) && JsonFile::TryLoad(fontPath, data) && data["atlasTexture"] == "";
		passed &= std::count_if(database.GetIssues().begin(), database.GetIssues().end(), [](const auto& issue) {
			return issue.type == AssetDatabaseIssueType::OrphanMeta || issue.type == AssetDatabaseIssueType::FontAtlasRepair;
		}) == 2;
		passed &= !database.RebuildMeta({ root, root / "missing" }) && database.Find(fontID) == font &&
			database.GetStructureRevision() == revision && !database.GetLastRebuildError().empty();

		// 検出後の元Asset復活を再確認する
		std::ofstream(root / "orphan.png") << "restored";
		passed &= !database.DeleteOrphanMeta(orphanPath) && std::filesystem::exists(orphanPath);
		std::filesystem::remove(root / "orphan.png");
		passed &= database.DeleteOrphanMeta(orphanPath) && !std::filesystem::exists(orphanPath);
		passed &= database.RepairFontAtlas(fontID, atlasID) && JsonFile::TryLoad(fontPath, data) &&
			data["atlasTexture"] == ToString(atlasID) && data["custom"] == 17;
		passed &= !database.RepairFontAtlas(fontID, atlasID);
		passed &= database.FindDependencies(fontID) == std::vector<AssetID>{ atlasID };

		// 独自拡張子と任意名Textureを同じ依存解析へ通す
		const auto effectPath = root / "test.effect";
		passed &= JsonFile::Save(effectPath, nlohmann::json{
			{ "model", ToString(atlasID) },
			{ "textureOverrides", { { "arbitraryName", ToString(atlasID) } } }
		});
		const auto effectID = database.ImportOrGet(RuntimePaths::ToAssetPath(effectPath), AssetType::ParticleEffect);
		passed &= effectID && database.FindDependencies(effectID) == std::vector<AssetID>{ atlasID };
		passed &= std::any_of(database.GetIssues().begin(), database.GetIssues().end(), [&](const auto& issue) {
			return issue.type == AssetDatabaseIssueType::ReferenceTypeMismatch && issue.assetID == effectID &&
				issue.referencedAssetID == atlasID && issue.expectedType == AssetType::Mesh;
		});
		std::ofstream(effectPath, std::ios::trunc) << "{";
		const bool rejected = !database.RefreshDependencies(effectID);
		passed &= rejected && database.FindDependencies(effectID) == std::vector<AssetID>{ atlasID };
		passed &= !database.RebuildMeta({ root }) && database.Find(fontID) == font;
		passed &= JsonFile::Save(effectPath, nlohmann::json::object());

		// meta保存失敗では索引へ新規公開しない
		const auto blockedPath = root / "blocked.png";
		std::ofstream(blockedPath) << "fixture";
		std::filesystem::create_directory(root / "blocked.png.meta");
		const auto beforeImport = database.GetStructureRevision();
		passed &= !database.ImportOrGet(RuntimePaths::ToAssetPath(blockedPath), AssetType::Texture) &&
			!database.FindByPath(RuntimePaths::ToAssetPath(blockedPath)) && database.GetStructureRevision() == beforeImport;
		std::filesystem::remove(root / "blocked.png.meta");
		passed &= static_cast<bool>(database.ImportOrGet(RuntimePaths::ToAssetPath(blockedPath), AssetType::Texture));
		passed &= database.GetStructureRevision() == beforeImport + 1;

		// 重複GUIDは旧索引を部分的に置き換えない
		const auto duplicatePath = root / "duplicate.png";
		std::ofstream(duplicatePath) << "fixture";
		AssetMeta duplicate = *database.Find(atlasID);
		passed &= AssetDatabase::WriteMetaFile(root / "duplicate.png.meta", duplicate);
		passed &= !database.RebuildMeta({ root }) && database.Find(fontID) == font;

		// 型の壊れたmetaは読込先と元ファイルを保持する
		const nlohmann::json metaSource = {{"custom", {{"enabled", true}}}, {"version", 99}};
		nlohmann::json metaDocument = {{"retained", true}};
		const auto retainedDocument = metaDocument;
		AssetMeta invalidMeta = duplicate;
		invalidMeta.importerVersion = 0;
		passed &= !AssetMetaStorage::BuildMetaDocument(invalidMeta, metaSource, metaDocument) &&
			metaDocument == retainedDocument;
		passed &= !AssetMetaStorage::BuildMetaDocument(duplicate, nlohmann::json::array(), metaDocument) &&
			metaDocument == retainedDocument;
		passed &= AssetMetaStorage::BuildMetaDocument(duplicate, metaSource, metaDocument) &&
			metaDocument["custom"] == metaSource["custom"] && metaDocument["guid"] == ToString(duplicate.guid) &&
			!metaDocument.contains("version") && metaSource.contains("version");

		const auto invalidPath = root / "invalid.png.meta";
		passed &= JsonFile::Save(invalidPath, nlohmann::json{ { "schemaVersion", "bad" } });
		AssetMeta unchanged = duplicate;
		passed &= !AssetDatabase::ReadMetaFile(invalidPath, unchanged) && unchanged.guid == duplicate.guid;
		std::ofstream(invalidPath, std::ios::trunc) << "{";
		passed &= !AssetDatabase::WriteMetaFile(invalidPath, duplicate);
		// 小数・負値・桁あふれを番号として受け付けない
		std::filesystem::remove(invalidPath);
		passed &= AssetDatabase::WriteMetaFile(invalidPath, duplicate) && JsonFile::TryLoad(invalidPath, data);
		for (const nlohmann::json invalidVersion : { nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json(4294967296ULL) }) {
			auto invalid = data;
			invalid["importerVersion"] = invalidVersion;
			passed &= JsonFile::Save(invalidPath, invalid) && !AssetDatabase::ReadMetaFile(invalidPath, unchanged);
		}
		auto invalidType = data;
		invalidType["type"] = "TextureTypo";
		passed &= JsonFile::Save(invalidPath, invalidType) && !AssetDatabase::ReadMetaFile(invalidPath, unchanged);
		passed &= !database.ImportOrGet(RuntimePaths::ToAssetPath(atlasPath), AssetType::Mesh);
		const auto beforeSettings = database.GetStructureRevision();
		passed &= database.UpdateImporterSettings(atlasID, nlohmann::json{ { "maxSize", 512 } }, 1) &&
			database.GetStructureRevision() == beforeSettings + 1;
		{
			TestFileReadLock lock(root / "test.png.meta");
			passed &= !database.UpdateImporterSettings(atlasID, nlohmann::json{ { "maxSize", 1024 } }, 1) &&
				database.Find(atlasID)->importerSettings["maxSize"] == 512;
		}
		return passed;
	}

	bool TestAssetDependencyCandidates() {

		using namespace Engine;
		const AssetID prefabID{ 1, 2 }, meshID{ 3, 4 }, audioID{ 5, 6 }, animationID{ 7, 8 }, textureID{ 9, 10 };
		const nlohmann::json data = {
			{ "PrefabAsset", ToString(prefabID) },
			{ "model", { { "guid", ToString(meshID) } } },
			{ "audio", { { "clip", ToString(audioID) }, { "playOnAwake", true }, { "volume", 1.0f } } },
			{ "state", { { "clip", ToString(animationID) }, { "wrapMode", "Loop" }, { "relativeTransform", false } } },
			{ "skinned", { { "clip", "Default" } } },
			{ "textures", { { "custom", ToString(textureID) } } },
			{ "opacityTexture", ToString(textureID) },
			{ "type", 4 }
		};
		AssetDependencyScanner::IDReferences candidates;
		AssetDependencyScanner::PathReferences paths;
		AssetDependencyScanner::ScanReferences(data, candidates, paths);
		const auto contains = [&](AssetID id, AssetType type) {
			const auto [begin, end] = candidates.equal_range(id);
			return std::any_of(begin, end, [type](const auto& candidate) { return candidate.second == type; });
		};
		bool passed = candidates.size() == 5 && paths.empty() && contains(prefabID, AssetType::Prefab) &&
			contains(meshID, AssetType::Mesh) && contains(audioID, AssetType::Audio) &&
			contains(animationID, AssetType::AnimationClip) && contains(textureID, AssetType::Texture);
		AssetDependencyScanner::ScanReferences({ { "mesh", ToString(textureID) }, { "texture", ToString(textureID) } }, candidates, paths);
		passed &= candidates.count(textureID) == 2 && contains(textureID, AssetType::Mesh) && contains(textureID, AssetType::Texture);
		return passed;
	}
}
