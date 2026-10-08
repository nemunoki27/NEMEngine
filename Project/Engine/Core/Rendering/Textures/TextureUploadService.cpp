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
#include <limits>
#include <stdexcept>
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

Engine::TextureUploadService::~TextureUploadService() {

	// 結果の保存先を破棄する前にワーカーを終了する
	Finalize();
}

void Engine::TextureUploadService::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

	Finalize();

	srvDescriptor_ = srvDescriptor;

	uploader_.Init(device, srvDescriptor);

	const uint32_t hardwareThreadCount = (std::max)(1u, std::thread::hardware_concurrency());
	const uint32_t threadCount = (std::min)(kMaxDecodeWorkerCount, hardwareThreadCount);
	decodeWorkers_.Start(threadCount, [this](DecodeRequest&& job, uint32_t workerIndex) {
		this->DecodeTextureWorker(std::move(job), workerIndex);
		});

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "TextureUploadServiceを開始します");
	Logger::Output(LogType::Engine, "Decode Worker数: {}", threadCount);
	Logger::EndSection(LogType::Engine);
}

void Engine::TextureUploadService::TickFinalize() {

	// アップロードジョブをスワップしてロックを解放する
	std::deque<CompletedRequest> jobs{};
	{
		std::scoped_lock lock(mutex_);
		jobs.swap(pendingUploads_);
	}

	// 記録されたジョブを処理する
	for (auto& completed : jobs) {

		auto& job = completed.texture;
		{
			std::scoped_lock lock(mutex_);
			// 更新前の画像を転送せず、最新要求へ進める
			if (!IsCurrentRequest(completed)) {
				queuedKeys_.erase(job.key);
				continue;
			}
		}

		GPUTextureResource uploaded{};
		// 単色テクスチャのアップロード
		if (job.isSolidColor) {

			uploaded = uploader_.UploadSolidColor1x1(job.solidRGBA[0], job.solidRGBA[1], job.solidRGBA[2], job.solidRGBA[3]);

		}
		// 画像テクスチャのアップロード
		else if (job.success) {
			uploaded = uploader_.UploadScratchImage(job.image, job.metadata);
		}

		// アップロード結果を反映する
		std::scoped_lock lock(mutex_);
		queuedKeys_.erase(job.key);
		// GPU転送中に変更された要求も公開しない
		if (!IsCurrentRequest(completed)) {
			if (uploaded.valid) {
				srvDescriptor_->Retire(uploaded.srvIndex, uploaded.resource);
			}
			continue;
		}

		// 再読込に失敗した場合は公開済みの画像を保持する
		if (readyTextures_.contains(job.key) && !uploaded.valid) {
			continue;
		}

		if (!uploaded.valid) {
			failedKeys_.insert(job.key);
			continue;
		}
		try {
			// 名前の準備も差し替え前に完了する
			uploaded.textureName = job.key;
			uploaded.resource->SetName(Algorithm::ConvertString(job.key).c_str());
			srvDescriptor_->UpdateResourceName(uploaded.srvIndex, uploaded.resource.Get());
			const auto desc = uploaded.resource->GetDesc();
			Logger::Output(LogType::Engine,
				"[TextureLoad][GPU] key={} srvIndex={} size={}x{} format={} status=準備完了",
				job.key, uploaded.srvIndex, static_cast<uint32_t>(desc.Width), desc.Height, static_cast<uint32_t>(desc.Format));
			auto existing = readyTextures_.find(job.key);
			if (existing != readyTextures_.end()) {
				// 使用中の旧DescriptorとResourceを一組で保持する
				srvDescriptor_->Retire(existing->second.srvIndex, existing->second.resource);
				existing->second = std::move(uploaded);
			} else {
				readyTextures_.emplace(job.key, std::move(uploaded));
			}
		} catch (...) {
			// 公開前の新Descriptorだけを戻す
			srvDescriptor_->Free(uploaded.srvIndex);
			throw;
		}
		failedKeys_.erase(job.key);
		++contentRevision_;
	}

	// 進行中の読込が終わったキーを最新要求で再投入する
	std::vector<DecodeRequest> deferredReloads{};
	{
		std::scoped_lock lock(mutex_);
		for (auto it = deferredReloadKeys_.begin(); it != deferredReloadKeys_.end();) {

			const auto request = keyRequests_.find(*it);
			if (request == keyRequests_.end()) {

				it = deferredReloadKeys_.erase(it);
				continue;
			}
			if (queuedKeys_.contains(*it)) {

				++it;
				continue;
			}

			DecodeRequest reloadDesc = request->second;
			queuedKeys_.insert(*it);
			deferredReloads.emplace_back(std::move(reloadDesc));
			it = deferredReloadKeys_.erase(it);
		}
	}
	for (const DecodeRequest& request : deferredReloads) {

		if (!QueueDecode(request)) {
			continue;
		}
		Logger::Output(LogType::Engine,
			"[TextureReload][遅延] key={} path={}", request.description.key, request.description.assetPath);
	}
}

void Engine::TextureUploadService::WaitAll() {

	for (;;) {
		decodeWorkers_.WaitIdle();
		TickFinalize();
		// 転送後に再投入した読込も完了まで待つ
		std::scoped_lock lock(mutex_);
		if (queuedKeys_.empty() && pendingUploads_.empty() && deferredReloadKeys_.empty()) {
			return;
		}
	}
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
		CompletedRequest completed;
		auto& job = completed.texture;
		job.key = key;
		job.isSolidColor = true;
		job.solidRGBA[0] = r;
		job.solidRGBA[1] = g;
		job.solidRGBA[2] = b;
		job.solidRGBA[3] = a;
		job.success = true;
		pendingUploads_.emplace_back(std::move(completed));
		accepted = true;
	}

	if (accepted) {

		Logger::Output(LogType::Engine,
			"[TextureLoad][待機列] key={} workers=MainThread processing=1 queued=0 rgba=[{},{},{},{}]",
			key, r, g, b, a);
	}
}

void Engine::TextureUploadService::RequestTextureFile(const TextureFileRequestDesc& desc) {

	std::vector<DecodeRequest> toEnqueue;
	{
		std::scoped_lock lock(mutex_);
		if (readyTextures_.contains(desc.key) || queuedKeys_.contains(desc.key)) {
			return;
		}

		// 再試行でも前の要求世代へ戻さない
		auto& request = keyRequests_[desc.key];
		request.description = desc;
		PrepareRequest(request, toEnqueue);
	}

	if (!QueueDecode(toEnqueue.front())) {
		return;
	}

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

	std::vector<DecodeRequest> toEnqueue;
	{
		std::scoped_lock lock(mutex_);
		// 元のファイルリクエストが無いキーはsolid colorや未ロードなので対象外
		auto it = keyRequests_.find(key);
		if (it == keyRequests_.end() || it->second.description.snapshotBytes) {
			return;
		}
		PrepareRequest(it->second, toEnqueue);
	}

	for (const DecodeRequest& request : toEnqueue) {
		if (!QueueDecode(request)) {
			continue;
		}
		Logger::Output(LogType::Engine, "[TextureReload][待機列] key={} path={}",
			request.description.key, request.description.assetPath);
	}
}

void Engine::TextureUploadService::RequestReloadByFile(
	const std::filesystem::path& fullPath,
	const TextureImportSettings* updatedSettings) {

	// 比較はlexically_normal+小文字化でWindowsの大小やセパレータ差を吸収する
	const std::wstring target = NormalizeFilePath(fullPath);
	if (target.empty()) {
		return;
	}

	std::vector<DecodeRequest> toEnqueue;
	{
		std::scoped_lock lock(mutex_);
		if (nextFileReloadRevision_ == std::numeric_limits<uint64_t>::max()) {
			throw std::overflow_error("Textureのファイル再読込世代が上限に達しました");
		}
		// 固定画像は所有元が新しい一組の要求へ置き換える
		fileReloadRevisions_[target] = nextFileReloadRevision_++;
		for (auto& [key, request] : keyRequests_) {

			auto& storedDesc = request.description;
			// 固定した世代を通常のファイル再読込で変更しない
			if (storedDesc.snapshotBytes) {
				continue;
			}
			// このキーが指すファイルの絶対パスを求めて変更ファイルと一致するか確認する
			const std::filesystem::path requestedPath = Algorithm::PathFromUTF8(storedDesc.assetPath);
			const std::filesystem::path candidate = requestedPath.is_absolute() ?
				requestedPath : RuntimePaths::ResolveAssetPath(storedDesc.assetPath);
			if (NormalizeFilePath(candidate) != target) {
				continue;
			}

			if (updatedSettings) {
				storedDesc.importSettings = *updatedSettings;
			}
			PrepareRequest(request, toEnqueue);
		}
	}

	for (const DecodeRequest& request : toEnqueue) {

		if (!QueueDecode(request)) {
			continue;
		}
		Logger::Output(LogType::Engine, "[TextureReload][待機列] key={} path={}",
			request.description.key, request.description.assetPath);
	}
}

void Engine::TextureUploadService::Finalize() {

	decodeWorkers_.Stop();

	if (srvDescriptor_) {
		for (auto& [key, texture] : readyTextures_) {
			if (texture.srvIndex != UINT32_MAX) {
				srvDescriptor_->Retire(texture.srvIndex, texture.resource);
			}
		}
	}
	{
		std::scoped_lock lock(mutex_);
		pendingUploads_.clear();
		if (!readyTextures_.empty()) ++contentRevision_;
		readyTextures_.clear();
		queuedKeys_.clear();
		failedKeys_.clear();
		deferredReloadKeys_.clear();
		keyRequests_.clear();
		fileReloadRevisions_.clear();
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

void Engine::TextureUploadService::DecodeTextureWorker(DecodeRequest&& request, uint32_t workerIndex) {

	const auto& job = request.description;
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
	pendingUploads_.push_back({ std::move(result), request.revision });
}

bool Engine::TextureUploadService::QueueDecode(const DecodeRequest& request) {

	if (decodeWorkers_.Enqueue(request)) {
		return true;
	}
	// 受付失敗を待機中として残さず旧Textureは保持する
	{
		std::scoped_lock lock(mutex_);
		const auto& key = request.description.key;
		queuedKeys_.erase(key);
		const auto latest = keyRequests_.find(key);
		if (latest != keyRequests_.end() && latest->second.revision == request.revision) {
			failedKeys_.insert(key);
			deferredReloadKeys_.erase(key);
		}
	}
	Logger::Output(LogType::Engine, spdlog::level::err, "[TextureLoad][受付失敗] key={}", request.description.key);
	return false;
}

void Engine::TextureUploadService::PrepareRequest(DecodeRequest& request, std::vector<DecodeRequest>& toEnqueue) {

	if (request.revision == UINT64_MAX) {
		throw std::overflow_error("Textureの要求世代が上限に達しました");
	}
	++request.revision;
	const auto& key = request.description.key;
	failedKeys_.erase(key);
	// 同じキーの読込を重ねず、最後の要求だけ再投入する
	if (queuedKeys_.contains(key)) {
		deferredReloadKeys_.insert(key);
		return;
	}
	toEnqueue.emplace_back(request);
	queuedKeys_.insert(key);
	deferredReloadKeys_.erase(key);
}

bool Engine::TextureUploadService::IsCurrentRequest(const CompletedRequest& completed) const {

	if (completed.texture.isSolidColor) {
		return true;
	}
	const auto latest = keyRequests_.find(completed.texture.key);
	return latest != keyRequests_.end() && latest->second.revision == completed.revision;
}
