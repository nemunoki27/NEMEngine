#include "MeshImportService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
// c++
#include <algorithm>
#include <utility>

//============================================================================
//	MeshImportService classMethods
//============================================================================

Engine::MeshImportService::~MeshImportService() {

	// 結果の保存先を破棄する前にワーカーを終了する
	Finalize();
}

void Engine::MeshImportService::Init(uint32_t threadCount) {

	// ワーカープールの開始
	workerPool_.Start((std::max)(1u, threadCount),
		[this](MeshLoadJob&& job, [[maybe_unused]] uint32_t workerIndex) { LoadJob(std::move(job)); });
}

void Engine::MeshImportService::Finalize() {

	// 結果の保存先を解放する前にworkerを停止する
	workerPool_.Stop();

	// 完了・待機・実行・失敗の状態を破棄する
	std::scoped_lock lock(mutex_);
	imported_.clear();
	queued_.clear();
	loading_.clear();
	failed_.clear();
}

bool Engine::MeshImportService::RequestLoadAsync(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	// 無効なIDは無視
	if (!meshAssetID) {
		return false;
	}
	{
		std::scoped_lock lock(mutex_);
		if (imported_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return false;
		}
	}

	// アセットデータベースからフルパスを解決して存在を確認
	std::filesystem::path fullPath = assetDatabase.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}
	const AssetMeta* meta = assetDatabase.Find(meshAssetID);
	const MeshImportSettings settings = meta ? ParseMeshImportSettings(meta->importerSettings) : MeshImportSettings{};
	std::array<std::filesystem::path, 3> manualLODPaths{};
	for (size_t index = 0; index < settings.manualLODMeshes.size(); ++index) {
		const AssetID manualAsset = settings.manualLODMeshes[index];
		const AssetMeta* manualMeta = manualAsset ? assetDatabase.Find(manualAsset) : nullptr;
		if (manualMeta && manualMeta->type == AssetType::Mesh && manualAsset != meshAssetID) {

			manualLODPaths[index] = assetDatabase.ResolveFullPath(manualAsset);
		}
	}

	{
		std::scoped_lock lock(mutex_);
		if (imported_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return false;
		}
		queued_.insert(meshAssetID);
		failed_.erase(meshAssetID);
	}

	// ジョブをワーカープールに追加
	if (!workerPool_.Enqueue(MeshLoadJob{
			.assetID = meshAssetID,
			.fullPath = std::move(fullPath),
			.settings = settings,
			.manualLODPaths = std::move(manualLODPaths),
		})) {
		// 受付を断った要求を待機中として残さない
		std::scoped_lock lock(mutex_);
		queued_.erase(meshAssetID);
		return false;
	}

	return true;
}

bool Engine::MeshImportService::TakeImported(AssetID meshAssetID, ImportedMeshAsset& outImported) {

	std::scoped_lock lock(mutex_);
	auto it = imported_.find(meshAssetID);
	if (it == imported_.end()) {
		return false;
	}
	outImported = std::move(it->second);
	imported_.erase(it);
	return true;
}

bool Engine::MeshImportService::ConsumeFailed(AssetID meshAssetID) {

	std::scoped_lock lock(mutex_);
	return failed_.erase(meshAssetID) != 0;
}

bool Engine::MeshImportService::IsPending(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	return queued_.contains(meshAssetID) || loading_.contains(meshAssetID);
}

bool Engine::MeshImportService::IsLoaded(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	return imported_.contains(meshAssetID);
}

void Engine::MeshImportService::WaitAll() {

	workerPool_.WaitIdle();
}

void Engine::MeshImportService::LoadJob(MeshLoadJob&& job) {

	// 要求を待機中から実行中へ移す
	{
		std::scoped_lock lock(mutex_);
		queued_.erase(job.assetID);
		loading_.insert(job.assetID);
	}

	// ファイルのインポート
	ImportedMeshAsset imported{};
	bool succeeded = false;
	try {
		imported = MeshFileImporter::ImportFile(job.assetID, job.fullPath, job.settings, job.manualLODPaths);
		succeeded = !imported.vertices.empty() && !imported.indices.empty();
	} catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Meshの非同期読み込み中に例外が発生しました path={} 内容={}",
			Algorithm::PathToUTF8(job.fullPath), exception.what());
		succeeded = false;
	} catch (...) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Meshの非同期読み込み中に不明な例外が発生しました path={}",
			Algorithm::PathToUTF8(job.fullPath));
		succeeded = false;
	}
	// 結果の保存
	{
		std::scoped_lock lock(mutex_);
		loading_.erase(job.assetID);
		if (succeeded) {
			imported_[job.assetID] = std::move(imported);
		} else {
			failed_.insert(job.assetID);
		}
	}
}
