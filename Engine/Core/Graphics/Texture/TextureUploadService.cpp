#include "TextureUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Debug/Logger.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	TextureUploadService classMethods
//============================================================================

namespace {

	// テクスチャメタからSRV記述子を構築する
	D3D12_SHADER_RESOURCE_VIEW_DESC BuildSRVDesc(const DirectX::TexMetadata& meta) {

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = meta.format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		switch (meta.dimension) {
		case DirectX::TEX_DIMENSION_TEXTURE1D: {
			//============================================================================
			//	1Dテクスチャ
			//============================================================================
			if (1 < meta.arraySize) {

				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				srvDesc.Texture1DArray.MostDetailedMip = 0;
				srvDesc.Texture1DArray.MipLevels = static_cast<UINT>(meta.mipLevels);
				srvDesc.Texture1DArray.FirstArraySlice = 0;
				srvDesc.Texture1DArray.ArraySize = static_cast<UINT>(meta.arraySize);
				srvDesc.Texture1DArray.ResourceMinLODClamp = 0.0f;
			} else {

				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
				srvDesc.Texture1D.MostDetailedMip = 0;
				srvDesc.Texture1D.MipLevels = static_cast<UINT>(meta.mipLevels);
				srvDesc.Texture1D.ResourceMinLODClamp = 0.0f;
			}
			break;
		}
		case DirectX::TEX_DIMENSION_TEXTURE2D: {
			//============================================================================
			//	2Dテクスチャ
			//============================================================================
			if (meta.IsCubemap()) {
				if (6 < meta.arraySize) {
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					srvDesc.TextureCubeArray.MostDetailedMip = 0;
					srvDesc.TextureCubeArray.MipLevels = static_cast<UINT>(meta.mipLevels);
					srvDesc.TextureCubeArray.First2DArrayFace = 0;
					srvDesc.TextureCubeArray.NumCubes = static_cast<UINT>(meta.arraySize / 6);
					srvDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
				} else {
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					srvDesc.TextureCube.MostDetailedMip = 0;
					srvDesc.TextureCube.MipLevels = static_cast<UINT>(meta.mipLevels);
					srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
				}
			} else if (1 < meta.arraySize) {
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Texture2DArray.MostDetailedMip = 0;
				srvDesc.Texture2DArray.MipLevels = static_cast<UINT>(meta.mipLevels);
				srvDesc.Texture2DArray.FirstArraySlice = 0;
				srvDesc.Texture2DArray.ArraySize = static_cast<UINT>(meta.arraySize);
				srvDesc.Texture2DArray.PlaneSlice = 0;
				srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
			} else {
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			}
			break;
		}
		case DirectX::TEX_DIMENSION_TEXTURE3D: {
			//============================================================================
			//	3Dテクスチャ
			//============================================================================
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			srvDesc.Texture3D.MostDetailedMip = 0;
			srvDesc.Texture3D.MipLevels = static_cast<UINT>(meta.mipLevels);
			srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
			break;
		}
		default:
			//============================================================================
			//	その他
			//============================================================================
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			break;
		}
		return srvDesc;
	}
}

void Engine::TextureUploadService::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

	Finalize();

	device_ = device;
	srvDescriptor_ = srvDescriptor;

	uploadCommand_ = std::make_unique<DxUploadCommand>();
	uploadCommand_->Create(device_);

	const uint32_t threadCount = (std::max)(1u, std::thread::hardware_concurrency());
	decodeWorkers_.Start(threadCount, [this](TextureFileRequestDesc&& job, uint32_t workerIndex) {
		this->DecodeTextureWorker(std::move(job), workerIndex);
		});

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "Start TextureUploadService");
	Logger::Output(LogType::Engine, "DecodeWorkerCount: {}", threadCount);
	Logger::EndSection(LogType::Engine);
}

void Engine::TextureUploadService::TickFinalize() {

	// アップロードジョブをスワップしてロックを解放する
	std::deque<PendingUploadJob> jobs{};
	{
		std::scoped_lock lock(mutex_);
		jobs.swap(pendingUploads_);
	}

	// 記録されたジョブを処理する
	for (auto& job : jobs) {

		GPUTextureResource uploaded{};
		// 単色テクスチャのアップロード
		if (job.isSolidColor) {

			uploaded = UploadSolidColor1x1(job.solidRGBA[0], job.solidRGBA[1], job.solidRGBA[2], job.solidRGBA[3]);

		}
		// 画像テクスチャのアップロード
		else if (job.success) {
			uploaded = UploadScratchImage(job.image, job.metadata);
		}

		// アップロード結果を反映する
		std::scoped_lock lock(mutex_);
		queuedKeys_.erase(job.key);
		failedKeys_.erase(job.key);
		auto it = readyTextures_.find(job.key);
		if (it != readyTextures_.end()) {
			if (srvDescriptor_ && it->second.srvIndex != UINT32_MAX) {
				srvDescriptor_->Free(it->second.srvIndex);
			}
			readyTextures_.erase(it);
		}
		// アップロードに成功していればマップに追加する
		if (uploaded.valid) {

			const auto desc = uploaded.resource->GetDesc();
			Logger::Output(LogType::Engine, "[TextureLoad][GPU] key={} srvIndex={} size={}x{} format={} status=READY",
				job.key, uploaded.srvIndex, static_cast<uint32_t>(desc.Width), desc.Height, static_cast<uint32_t>(desc.Format));

			uploaded.textureName = job.key;
			readyTextures_.emplace(job.key, std::move(uploaded));
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
		PendingUploadJob job{};
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

		Logger::Output(LogType::Engine, "[TextureLoad][Queue ] key={} workers=MainThread processing=1 queued=0 rgba=[{},{},{},{}]",
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
		shouldEnqueue = true;
	}
	if (!shouldEnqueue) {
		return;
	}

	decodeWorkers_.Enqueue(desc);

	const auto stats = decodeWorkers_.GetStats();
	Logger::Output(LogType::Engine, "[TextureLoad][Queue] key={} workers={} processing={} queued={} path={}",
		desc.key, stats.threadCount, stats.inFlightCount, stats.queuedCount, desc.assetPath);
}

void Engine::TextureUploadService::RequestTextureFile(const std::string& key, const std::string& assetPath) {

	// キーが空でないことと、すでに同じキーのテクスチャが存在しないことを確認する
	TextureFileRequestDesc desc{};
	desc.key = key;
	desc.assetPath = assetPath;
	RequestTextureFile(desc);
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
	}

	uploadCommand_.reset();
	srvDescriptor_ = nullptr;
	device_ = nullptr;
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
	Logger::Output(LogType::Engine, "[TextureLoad][Start] Worker[{}/{}] processing={} queued={} key={} path={}",
		workerIndex + 1, startStats.threadCount, startStats.inFlightCount, startStats.queuedCount, job.key, job.assetPath);

	PendingUploadJob result{};
	result.key = job.key;

	// ファイルパスからテクスチャをデコードする
	const std::filesystem::path fullPath(job.assetPath);
	const std::string extension = Algorithm::ToLower(fullPath.extension().string());
	const std::wstring fullPathW = Algorithm::ConvertString(fullPath.string());

	DirectX::ScratchImage loaded{};
	HRESULT hr = E_FAIL;
	if (extension == ".dds") {
		hr = DirectX::LoadFromDDSFile(fullPathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, loaded);
	} else {
		hr = DirectX::LoadFromWICFile(fullPathW.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, loaded);
	}
	if (SUCCEEDED(hr)) {

		auto meta = loaded.GetMetadata();
		result.image = std::move(loaded);
		result.metadata = result.image.GetMetadata();
		result.success = true;
	}
	const auto finishStats = decodeWorkers_.GetStats();
	uint32_t remainingProcessing = (0 < finishStats.inFlightCount) ? (finishStats.inFlightCount - 1) : 0;

	if (result.success) {

		Logger::Output(LogType::Engine, "[TextureLoad][Finish] Worker[{}/{}] processing={} queued={} key={} size={}x{} mips={} format={} status=OK",
			workerIndex + 1, finishStats.threadCount, remainingProcessing, finishStats.queuedCount, job.key,
			static_cast<uint32_t>(result.metadata.width), static_cast<uint32_t>(result.metadata.height),
			static_cast<uint32_t>(result.metadata.mipLevels), static_cast<uint32_t>(result.metadata.format));
	} else {

		Logger::Output(LogType::Engine, spdlog::level::warn, "[TextureLoad][Finish] Worker[{}/{}] processing={} queued={} key={} status=FAILED hr=0x{:08X} path={}",
			workerIndex + 1, finishStats.threadCount, remainingProcessing, finishStats.queuedCount, job.key, static_cast<uint32_t>(hr), job.assetPath);
	}
	std::scoped_lock lock(mutex_);
	pendingUploads_.emplace_back(std::move(result));
}

Engine::GPUTextureResource Engine::TextureUploadService::UploadSolidColor1x1(
	uint8_t r, uint8_t g, uint8_t b, uint8_t a) {

	GPUTextureResource result{};
	const uint8_t pixel[4] = { r, g, b, a };

	// 1x1のテクスチャリソースを作成する
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 1;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	D3D12_HEAP_PROPERTIES defaultHeap{};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	// デフォルトヒープにテクスチャリソースを作成する
	HRESULT hr = device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result.resource));
	if (FAILED(hr)) {
		return result;
	}

	// ピクセルデータをサブリソース構造体にセットする
	D3D12_SUBRESOURCE_DATA subResource{};
	subResource.pData = pixel;
	subResource.RowPitch = 4;
	subResource.SlicePitch = 4;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(result.resource.Get(), 0, 1);

	// アップロード用のバッファを作成する
	ComPtr<ID3D12Resource> uploadBuffer = nullptr;
	D3D12_HEAP_PROPERTIES uploadHeap{};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = uploadBufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// アップロード用バッファをコミットリソースとして作成する
	hr = device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
	if (FAILED(hr)) {
		return result;
	}

	// コマンドリストを取得して、サブリソースデータをアップロードする
	ID3D12GraphicsCommandList* commandList = uploadCommand_->GetCommandList();
	UpdateSubresources(commandList, result.resource.Get(), uploadBuffer.Get(), 0, 0, 1, &subResource);

	// コピー後のリソースバリアを設定する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = result.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// コマンドを実行する
	uploadCommand_->ExecuteCommands();

	// SRVを作成する
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDescriptor_->CreateSRV(result.srvIndex, result.resource.Get(), srvDesc);
	result.gpuHandle = srvDescriptor_->GetGPUHandle(result.srvIndex);
	result.valid = true;
	return result;
}

Engine::GPUTextureResource Engine::TextureUploadService::UploadScratchImage(
	const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta) {

	GPUTextureResource result{};
	if (!image.GetImages() || image.GetImageCount() == 0) {
		return result;
	}

	// テクスチャリソースを作成する
	D3D12_RESOURCE_DESC desc{};
	switch (meta.dimension) {
	case DirectX::TEX_DIMENSION_TEXTURE1D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		desc.DepthOrArraySize = static_cast<UINT16>(meta.arraySize);
		break;
	case DirectX::TEX_DIMENSION_TEXTURE2D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.DepthOrArraySize = static_cast<UINT16>(meta.arraySize);
		break;
	case DirectX::TEX_DIMENSION_TEXTURE3D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		desc.DepthOrArraySize = static_cast<UINT16>(meta.depth);
		break;
	default:
		return result;
	}
	desc.Width = static_cast<UINT64>(meta.width);
	desc.Height = static_cast<UINT>(meta.height);
	desc.MipLevels = static_cast<UINT16>(meta.mipLevels);
	desc.Format = meta.format;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	D3D12_HEAP_PROPERTIES defaultHeap{};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	// デフォルトヒープにテクスチャリソースを作成する
	HRESULT hr = device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result.resource));
	if (FAILED(hr)) {
		return result;
	}

	// サブリソース構造体の配列を作成して、画像データをセットする
	std::vector<D3D12_SUBRESOURCE_DATA> subResources{};
	DirectX::PrepareUpload(device_, image.GetImages(), image.GetImageCount(), meta, subResources);

	// アップロードに必要なバッファサイズを取得する
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(result.resource.Get(), 0, static_cast<UINT>(subResources.size()));

	// アップロード用のバッファを作成する
	ComPtr<ID3D12Resource> uploadBuffer = nullptr;
	D3D12_HEAP_PROPERTIES uploadHeap{};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = uploadBufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// アップロード用バッファをコミットリソースとして作成する
	hr = device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
	if (FAILED(hr)) {
		return result;
	}

	// コマンドリストを取得して、サブリソースデータをアップロードする
	ID3D12GraphicsCommandList* commandList = uploadCommand_->GetCommandList();
	UpdateSubresources(commandList, result.resource.Get(), uploadBuffer.Get(), 0, 0, static_cast<UINT>(subResources.size()), subResources.data());

	// コピー後のリソースバリアを設定する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = result.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// コマンドを実行する
	uploadCommand_->ExecuteCommands();

	// SRVを作成する
	const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = BuildSRVDesc(meta);
	srvDescriptor_->CreateSRV(result.srvIndex, result.resource.Get(), srvDesc);
	result.gpuHandle = srvDescriptor_->GetGPUHandle(result.srvIndex);
	result.valid = true;
	return result;
}