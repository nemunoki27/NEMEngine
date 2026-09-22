#include "TextureUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <deque>
#include <thread>
#include <vector>
// directX
#include <DirectXMath.h>

//============================================================================
//	TextureUploadService classMethods
//============================================================================
namespace {

	constexpr uint32_t kMaxDecodeWorkerCount = 4;
}

void Engine::TextureUploadService::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor, ID3D12CommandQueue* graphicsQueue) {

	Finalize();

	srvDescriptor_ = srvDescriptor;

	uploader_.Init(device, srvDescriptor, graphicsQueue);

	const uint32_t hardwareThreadCount = (std::max)(1u, std::thread::hardware_concurrency());
	const uint32_t threadCount = (std::min)(kMaxDecodeWorkerCount, hardwareThreadCount);
	decodeWorkers_.Start(threadCount, [this](TextureFileRequestDesc&& job, uint32_t workerIndex) {
		this->DecodeTextureWorker(std::move(job), workerIndex);
		});

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "TextureUploadServiceを開始します");
	Logger::Output(LogType::Engine, "Decode Worker数: {}", threadCount);
	Logger::EndSection(LogType::Engine);
}

void Engine::TextureUploadService::TickFinalize() {

	// アップロードジョブをスワップしてロックを解放する
	std::deque<DecodedTexture> jobs{};
	{
		std::scoped_lock lock(mutex_);
		jobs.swap(pendingUploads_);
	}

	// 記録されたジョブを処理する
	for (auto& job : jobs) {

		// reload時は既存のSRVインデックスを再利用して差し替える、index据え置きなのでバッチキャッシュ等の参照を壊さない
		uint32_t reuseSrvIndex = UINT32_MAX;
		if (job.reload) {
			std::scoped_lock lock(mutex_);
			auto existing = readyTextures_.find(job.key);
			if (existing != readyTextures_.end() && existing->second.srvIndex != UINT32_MAX) {
				reuseSrvIndex = existing->second.srvIndex;
			}
		}

		GPUTextureResource uploaded{};
		// 単色テクスチャのアップロード
		if (job.isSolidColor) {

			uploaded = uploader_.UploadSolidColor1x1(job.solidRGBA[0], job.solidRGBA[1], job.solidRGBA[2], job.solidRGBA[3]);

		}
		// 画像テクスチャのアップロード
		else if (job.success) {
			uploaded = uploader_.UploadScratchImage(job.image, job.metadata, reuseSrvIndex);
		}

		// アップロード結果を反映する
		std::scoped_lock lock(mutex_);
		queuedKeys_.erase(job.key);

		// reloadのデコードに失敗した時は、書き込み途中や一時的な破損なので既存の有効なテクスチャを壊さず保持する
		if (job.reload && !uploaded.valid) {
			continue;
		}

		failedKeys_.erase(job.key);
		auto it = readyTextures_.find(job.key);
		if (it != readyTextures_.end()) {
			// 再利用indexで上書きした時は同じindexなのでFreeしない、別indexになった時のみ旧indexを解放する
			if (srvDescriptor_ && it->second.srvIndex != UINT32_MAX && it->second.srvIndex != uploaded.srvIndex) {
				srvDescriptor_->Free(it->second.srvIndex);
			}
			readyTextures_.erase(it);
		}
		// アップロードに成功していればマップに追加する
		if (uploaded.valid) {

			const auto desc = uploaded.resource->GetDesc();
			Logger::Output(LogType::Engine,
				"[TextureLoad][GPU] key={} srvIndex={} size={}x{} format={} status=準備完了",
				job.key, uploaded.srvIndex, static_cast<uint32_t>(desc.Width), desc.Height, static_cast<uint32_t>(desc.Format));

			uploaded.textureName = job.key;
			uploaded.resource->SetName(Algorithm::ConvertString(job.key).c_str());
			if (srvDescriptor_ && uploaded.srvIndex != UINT32_MAX) {
				srvDescriptor_->UpdateResourceName(uploaded.srvIndex, uploaded.resource.Get());
			}
			readyTextures_.emplace(job.key, std::move(uploaded));
		} else {

			failedKeys_.insert(job.key);
		}
	}

	// 読み込み中にImporter設定が変わったキーは初回転送直後に最新設定で再読込する
	std::vector<TextureFileRequestDesc> deferredReloads{};
	{
		std::scoped_lock lock(mutex_);
		for (auto it = deferredReloadKeys_.begin(); it != deferredReloadKeys_.end();) {

			const auto request = keyRequests_.find(*it);
			if (request == keyRequests_.end() || failedKeys_.contains(*it)) {

				it = deferredReloadKeys_.erase(it);
				continue;
			}
			if (queuedKeys_.contains(*it) || !readyTextures_.contains(*it)) {

				++it;
				continue;
			}

			TextureFileRequestDesc reloadDesc = request->second;
			reloadDesc.reload = true;
			queuedKeys_.insert(*it);
			deferredReloads.emplace_back(std::move(reloadDesc));
			it = deferredReloadKeys_.erase(it);
		}
	}
	for (TextureFileRequestDesc& desc : deferredReloads) {

		decodeWorkers_.Enqueue(desc);
		Logger::Output(LogType::Engine,
			"[TextureReload][遅延] key={} path={}", desc.key, desc.assetPath);
	}
}

void Engine::TextureUploadService::WaitAll() {

	decodeWorkers_.WaitIdle();
	TickFinalize();
}

void Engine::TextureUploadService::RequestSolidColor1x1(
	const std::string& key, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {

	if (key.empty()) {
		return;
	}

	bool accepted = false;
	{
		std::scoped_lock lock(mutex_);
		if (readyTextures_.contains(key) || queuedKeys_.contains(key)) {
			return;
		}
		queuedKeys_.insert(key);

		// アップロードジョブを記録する
		DecodedTexture job{};
		job.key = key;
		job.isSolidColor = true;
		job.solidRGBA[0] = r;
		job.solidRGBA[1] = g;
		job.solidRGBA[2] = b;
		job.solidRGBA[3] = a;
		job.success = true;
		pendingUploads_.emplace_back(std::move(job));
		accepted = true;
	}

	if (accepted) {

		Logger::Output(LogType::Engine,
			"[TextureLoad][待機列] key={} workers=MainThread processing=1 queued=0 rgba=[{},{},{},{}]",
			key, r, g, b, a);
	}
}

void Engine::TextureUploadService::RequestTextureFile(const TextureFileRequestDesc& desc) {

	bool shouldEnqueue = false;
	{
		std::scoped_lock lock(mutex_);
		if (readyTextures_.contains(desc.key) || queuedKeys_.contains(desc.key)) {
			return;
		}

		// 再試行可能にする
		failedKeys_.erase(desc.key);
		queuedKeys_.insert(desc.key);
		// 再デコード用に元リクエストを覚えておく、reloadフラグは持ち越さない
		TextureFileRequestDesc stored = desc;
		stored.reload = false;
		keyRequests_[desc.key] = std::move(stored);
		shouldEnqueue = true;
	}
	if (!shouldEnqueue) {
		return;
	}

	decodeWorkers_.Enqueue(desc);

	const auto stats = decodeWorkers_.GetStats();
	Logger::Output(LogType::Engine, "[TextureLoad][待機列] key={} workers={} processing={} queued={} path={}",
		desc.key, stats.threadCount, stats.inFlightCount, stats.queuedCount, desc.assetPath);
}

void Engine::TextureUploadService::RequestTextureFile(const std::string& key, const std::string& assetPath) {

	// キーが空でないことと、すでに同じキーのテクスチャが存在しないことを確認する
	TextureFileRequestDesc desc{};
	desc.key = key;
	desc.assetPath = assetPath;
	RequestTextureFile(desc);
}

void Engine::TextureUploadService::RequestReload(const std::string& key) {

	TextureFileRequestDesc desc{};
	{
		std::scoped_lock lock(mutex_);
		// 元のファイルリクエストが無いキーはsolid colorや未ロードなので対象外
		auto it = keyRequests_.find(key);
		if (it == keyRequests_.end()) {
			return;
		}
		// まだロードされていないキーや、既に再ロードが進行中のキーは重ねて投げない
		if (!readyTextures_.contains(key) || queuedKeys_.contains(key)) {
			return;
		}

		desc = it->second;
		desc.reload = true;
		queuedKeys_.insert(key);
	}

	decodeWorkers_.Enqueue(desc);
	Logger::Output(LogType::Engine, "[TextureReload][待機列] key={} path={}", desc.key, desc.assetPath);
}

void Engine::TextureUploadService::RequestReloadByFile(
	const std::filesystem::path& fullPath,
	const TextureImportSettings* updatedSettings) {

	// 比較はlexically_normal+小文字化でWindowsの大小やセパレータ差を吸収する
	const std::wstring target = Algorithm::ToLowerW(fullPath.lexically_normal().generic_wstring());
	if (target.empty()) {
		return;
	}

	std::vector<TextureFileRequestDesc> toEnqueue;
	{
		std::scoped_lock lock(mutex_);
		for (auto& [key, storedDesc] : keyRequests_) {

			// このキーが指すファイルの絶対パスを求めて変更ファイルと一致するか確認する
			const std::filesystem::path requestedPath = Algorithm::PathFromUTF8(storedDesc.assetPath);
			const std::filesystem::path candidate = requestedPath.is_absolute() ?
				requestedPath : RuntimePaths::ResolveAssetPath(storedDesc.assetPath);
			if (Algorithm::ToLowerW(candidate.lexically_normal().generic_wstring()) != target) {
				continue;
			}

			if (updatedSettings) {
				storedDesc.importSettings = *updatedSettings;
			}
			storedDesc.reload = false;
			if (queuedKeys_.contains(key)) {

				deferredReloadKeys_.insert(key);
				continue;
			}
			if (!readyTextures_.contains(key)) {
				continue;
			}

			TextureFileRequestDesc reloadDesc = storedDesc;
			reloadDesc.reload = true;
			queuedKeys_.insert(key);
			toEnqueue.emplace_back(std::move(reloadDesc));
		}
	}

	for (TextureFileRequestDesc& desc : toEnqueue) {

		decodeWorkers_.Enqueue(desc);
		Logger::Output(LogType::Engine, "[TextureReload][待機列] key={} path={}", desc.key, desc.assetPath);
	}
}

void Engine::TextureUploadService::Finalize() {

	decodeWorkers_.Stop();

	if (srvDescriptor_) {
		for (auto& [key, texture] : readyTextures_) {
			if (texture.srvIndex != UINT32_MAX) {
				srvDescriptor_->Free(texture.srvIndex);
			}
		}
	}
	{
		std::scoped_lock lock(mutex_);
		pendingUploads_.clear();
		readyTextures_.clear();
		queuedKeys_.clear();
		failedKeys_.clear();
		deferredReloadKeys_.clear();
		keyRequests_.clear();
	}

	uploader_.Finalize();
	srvDescriptor_ = nullptr;
}

const Engine::GPUTextureResource* Engine::TextureUploadService::GetTexture(const std::string& key) const {

	std::scoped_lock lock(mutex_);
	auto it = readyTextures_.find(key);
	return (it == readyTextures_.end()) ? nullptr : &it->second;
}

Engine::TextureRequestState Engine::TextureUploadService::GetState(const std::string& key) const {

	std::scoped_lock lock(mutex_);

	// テクスチャの情報に応じて状態を返す
	if (readyTextures_.contains(key)) {
		return TextureRequestState::Ready;
	}
	if (queuedKeys_.contains(key)) {
		return TextureRequestState::Queued;
	}
	if (failedKeys_.contains(key)) {
		return TextureRequestState::Failed;
	}
	return TextureRequestState::None;
}

void Engine::TextureUploadService::DecodeTextureWorker(TextureFileRequestDesc&& job, uint32_t workerIndex) {

	const auto startStats = decodeWorkers_.GetStats();
	Logger::Output(LogType::Engine, "[TextureLoad][開始] Worker[{}/{}] processing={} queued={} key={} path={}",
		workerIndex + 1, startStats.threadCount, startStats.inFlightCount, startStats.queuedCount, job.key, job.assetPath);

	DecodedTexture result = TextureDecoder::Decode(job);
	const auto finishStats = decodeWorkers_.GetStats();
	uint32_t remainingProcessing = (0 < finishStats.inFlightCount) ? (finishStats.inFlightCount - 1) : 0;

	if (result.success) {

		Logger::Output(LogType::Engine,
			"[TextureLoad][完了] Worker[{}/{}] processing={} queued={} "
			"key={} size={}x{} mips={} format={} status=成功",
			workerIndex + 1, finishStats.threadCount, remainingProcessing, finishStats.queuedCount, job.key,
			static_cast<uint32_t>(result.metadata.width), static_cast<uint32_t>(result.metadata.height),
			static_cast<uint32_t>(result.metadata.mipLevels), static_cast<uint32_t>(result.metadata.format));
	} else {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[TextureLoad][完了] Worker[{}/{}] processing={} queued={} "
			"key={} status=失敗 stage={} hr=0x{:08X} path={}",
			workerIndex + 1, finishStats.threadCount, remainingProcessing,
			finishStats.queuedCount, job.key, result.failureStage,
			static_cast<uint32_t>(result.result), job.assetPath);
	}
	std::scoped_lock lock(mutex_);
	pendingUploads_.emplace_back(std::move(result));
}
