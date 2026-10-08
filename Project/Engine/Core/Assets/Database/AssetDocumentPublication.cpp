#include "AssetDocumentPublication.h"

//============================================================================
//	include
//============================================================================
#include "AssetDatabase.h"
#include "AssetMetaStorage.h"
#include "AssetDocumentRecovery.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <stdexcept>
#include <unordered_set>
#include <utility>

//============================================================================
//	AssetDocumentPublication classMethods
//============================================================================
bool Engine::AssetDocumentPublication::Prepare(const AssetDatabase& database, const std::string& assetPath, AssetType type,
	AssetDocumentChange& out, std::string& diagnostic) {

	diagnostic.clear();
	try {
		// 未完了保存の内容を新しい編集で上書きしない
		for (const auto kind : AssetDocumentRecovery::GetSaveKinds()) {
			const auto pending = JsonFileJournal::GetRecoveries(AssetDocumentRecovery::MakeScope(kind), true);
			if (!pending.empty()) {
				diagnostic = "未完了のAsset保存があります Projectの診断画面で復旧を確認してください";
				return false;
			}
		}
		// 書込前の文書とmetaの状態を保持する
		AssetDocumentChange next;
		next.filePath = database.ResolveAssetPath(assetPath);
		if (next.filePath.empty()) {
			diagnostic = "Assetの保存先を解決できません";
			return false;
		}
		const auto metaPath = AssetMetaStorage::MetaPathOf(next.filePath);
		next.fileRevision = StorageFileUtility::FileRevision(next.filePath);
		next.metaRevision = StorageFileUtility::FileRevision(metaPath);
		const AssetMeta* indexed = database.FindByPath(assetPath);
		nlohmann::json source = nlohmann::json::object();
		if (std::filesystem::exists(metaPath)) {
			// 破損したmetaや別のGUIDを上書きしない
			if (!AssetMetaStorage::ReadMetaFile(metaPath, next.metadata) || !JsonFile::TryLoad(metaPath, source)) {
				diagnostic = "保存先のmetaを読み込めません";
				return false;
			}
			if (indexed && indexed->guid != next.metadata.guid) {
				diagnostic = "保存先のmetaと索引のGUIDが一致しません";
				return false;
			}
		} else if (indexed) {
			next.metadata = *indexed;
		} else {
			// 新規Assetの参照を保存前に確定する
			next.metadata.guid = AssetGUID::New();
			next.metadata.importer = AssetMetaStorage::ResolveImporterName(type);
		}
		if (next.metadata.type != AssetType::Unknown && next.metadata.type != type) {
			diagnostic = "保存先のAsset種別が一致しません";
			return false;
		}
		if (const auto existing = database.Find(next.metadata.guid); existing && existing != indexed) {
			diagnostic = "保存先のGUIDが別のAssetと重複しています";
			return false;
		}
		next.metadata.assetPath = assetPath;
		next.metadata.type = type;
		if (!AssetMetaStorage::BuildMetaDocument(next.metadata, source, next.metaDocument)) {
			diagnostic = "保存するmeta文書を作成できません";
			return false;
		}
		if (StorageFileUtility::FileRevision(next.filePath) != next.fileRevision ||
			StorageFileUtility::FileRevision(metaPath) != next.metaRevision) {
			diagnostic = "保存先の準備中に外部変更を検出しました";
			return false;
		}
		out = std::move(next);
		return true;
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

bool Engine::AssetDocumentPublication::Commit(AssetDatabase& database, std::span<const AssetDocumentChange> changes,
	const JsonFileJournal::Scope& scope, std::string& diagnostic) {

	diagnostic.clear();
	try {
		// 公開中の索引を変更せず登録結果を組み立てる
		if (changes.empty()) {
			return true;
		}
		AssetDatabase prepared = database;
		std::vector<JsonFileChange> documents;
		std::unordered_set<AssetID> identifiers;
		for (const auto& change : changes) {
			if (!change.metadata.guid || !identifiers.emplace(change.metadata.guid).second ||
				change.filePath != database.ResolveAssetPath(change.metadata.assetPath)) {
				diagnostic = "保存するAssetの識別子かパスが不正です";
				return false;
			}
			documents.push_back({change.filePath, change.document, false, change.canonicalize, change.bytes});
			documents.push_back({AssetMetaStorage::MetaPathOf(change.filePath), change.metaDocument, false, true});
		}
		const auto recover = [&scope](const std::filesystem::path& directory, std::string& error) {
			return JsonFileJournal::Recover(scope, directory, error, [](const std::filesystem::path&) {});
		};
		const auto checkBefore = [&]() {
			// 排他を得てから準備時の状態を照合する
			for (const auto& change : changes) {
				if (StorageFileUtility::FileRevision(change.filePath) != change.fileRevision ||
					StorageFileUtility::FileRevision(AssetMetaStorage::MetaPathOf(change.filePath)) != change.metaRevision) {
					throw std::runtime_error("保存前に外部変更を検出しました");
				}
			}
		};
		const auto checkAfter = [&]() {
			// 全文書を読み取れる状態で参照と索引を検証する
			for (const auto& change : changes) {
				AssetMeta stored;
				if (!AssetMetaStorage::ReadMetaFile(AssetMetaStorage::MetaPathOf(change.filePath), stored) ||
					stored.guid != change.metadata.guid || stored.type != change.metadata.type) {
					throw std::runtime_error("保存したmetaと準備済みの識別子が一致しません");
				}
				if (prepared.ImportOrGet(change.metadata.assetPath, change.metadata.type) != change.metadata.guid) {
					throw std::runtime_error("保存したAssetを索引へ登録できません");
				}
			}
			for (const auto& change : changes) {
				if (!prepared.RefreshDependencies(change.metadata.guid)) {
					throw std::runtime_error("保存したAssetの参照を解析できません");
				}
				prepared.NotifyContentChanged(change.metadata.guid);
			}
		};
		if (!JsonFileJournal::Commit(scope, documents, "Asset文書の保存", diagnostic, recover, checkBefore, checkAfter)) {
			return false;
		}
		// ファイルの確定後に完成した索引だけを公開する
		SwapDatabaseState(database, prepared);
		return true;
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

void Engine::AssetDocumentPublication::SwapDatabaseState(AssetDatabase& database, AssetDatabase& prepared) noexcept {

	// 旧索引の借用と派生cacheを同時に失効させる
	database.cacheLifetime_.identity.swap(prepared.cacheLifetime_.identity);
	database.projectRoot_.swap(prepared.projectRoot_);
	database.assetsRoot_.swap(prepared.assetsRoot_);
	database.guidToMeta_.swap(prepared.guidToMeta_);
	database.pathToGuid_.swap(prepared.pathToGuid_);
	database.referencersByGuid_.swap(prepared.referencersByGuid_);
	database.issues_.swap(prepared.issues_);
	std::swap(database.structureRevision_, prepared.structureRevision_);
	std::swap(database.contentRevision_, prepared.contentRevision_);
	database.contentRevisions_.swap(prepared.contentRevisions_);
	database.lastRebuildError_.swap(prepared.lastRebuildError_);
	database.scanRoots_.swap(prepared.scanRoots_);
	std::swap(database.buildingIndex_, prepared.buildingIndex_);
}
