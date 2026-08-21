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

	// 読み込み要求に応じたWICフラグを取得する
	DirectX::WIC_FLAGS ResolveWICFlags(Engine::TextureColorSpace colorSpace) {

		switch (colorSpace) {
		case Engine::TextureColorSpace::SRGB:
			return DirectX::WIC_FLAGS_FORCE_SRGB;
		case Engine::TextureColorSpace::Linear:
			return DirectX::WIC_FLAGS_IGNORE_SRGB;
		case Engine::TextureColorSpace::Auto:
		default:
			return DirectX::WIC_FLAGS_NONE;
		}
	}

	// DDSやTGAのフォーマットへ明示色空間を反映する
	void OverrideColorSpace(DirectX::ScratchImage& image,
		Engine::TextureColorSpace colorSpace) {

		if (colorSpace == Engine::TextureColorSpace::Auto) {
			return;
		}
		const DXGI_FORMAT source = image.GetMetadata().format;
		const DXGI_FORMAT target = colorSpace == Engine::TextureColorSpace::SRGB ?
			DirectX::MakeSRGB(source) : DirectX::MakeLinear(source);
		if (target != DXGI_FORMAT_UNKNOWN && target != source) {
			image.OverrideFormat(target);
		}
	}

	// 近傍の不透明色を透明ピクセルへ伝播して線形補間時の色滲みを防ぐ
	bool BleedTransparentPixels(DirectX::ScratchImage& image) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
			metadata.arraySize != 1 || metadata.mipLevels != 1) {
			return false;
		}

		const bool sRGB = DirectX::IsSRGB(metadata.format);
		DirectX::ScratchImage converted{};
		const DXGI_FORMAT format = sRGB ?
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		DirectX::ScratchImage* workingImage = &image;
		if (metadata.format != format) {

			const HRESULT hr = DirectX::Convert(
				image.GetImages(), image.GetImageCount(), metadata,
				format, DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr)) {
				return false;
			}
			workingImage = &converted;
		}

		const DirectX::Image* base = workingImage->GetImage(0, 0, 0);
		if (!base || base->width == 0 || base->height == 0) {
			return false;
		}

		const size_t pixelCount = base->width * base->height;
		std::vector<int32_t> source(pixelCount, -1);
		std::deque<size_t> pending{};
		for (size_t y = 0; y < base->height; ++y) {

			const uint8_t* row = base->pixels + y * base->rowPitch;
			for (size_t x = 0; x < base->width; ++x) {

				const size_t index = y * base->width + x;
				if (row[x * 4 + 3] != 0) {
					source[index] = static_cast<int32_t>(index);
					pending.emplace_back(index);
				}
			}
		}
		if (pending.empty() || pending.size() == pixelCount) {
			if (workingImage == &converted) {
				image = std::move(converted);
			}
			return true;
		}

		constexpr int32_t kOffsetX[4]{ -1, 1, 0, 0 };
		constexpr int32_t kOffsetY[4]{ 0, 0, -1, 1 };
		while (!pending.empty()) {

			const size_t index = pending.front();
			pending.pop_front();
			const int32_t x = static_cast<int32_t>(index % base->width);
			const int32_t y = static_cast<int32_t>(index / base->width);
			for (uint32_t direction = 0; direction < 4; ++direction) {

				const int32_t nextX = x + kOffsetX[direction];
				const int32_t nextY = y + kOffsetY[direction];
				if (nextX < 0 || nextY < 0 ||
					nextX >= static_cast<int32_t>(base->width) ||
					nextY >= static_cast<int32_t>(base->height)) {
					continue;
				}

				const size_t next = static_cast<size_t>(nextY) * base->width +
					static_cast<size_t>(nextX);
				if (source[next] >= 0) {
					continue;
				}
				source[next] = source[index];
				pending.emplace_back(next);
			}
		}

		for (size_t y = 0; y < base->height; ++y) {

			uint8_t* row = base->pixels + y * base->rowPitch;
			for (size_t x = 0; x < base->width; ++x) {

				uint8_t* pixel = row + x * 4;
				if (pixel[3] != 0) {
					continue;
				}
				const size_t index = y * base->width + x;
				const size_t colorIndex = static_cast<size_t>(source[index]);
				const size_t colorX = colorIndex % base->width;
				const size_t colorY = colorIndex / base->width;
				const uint8_t* color = base->pixels + colorY * base->rowPitch + colorX * 4;
				pixel[0] = color[0];
				pixel[1] = color[1];
				pixel[2] = color[2];
			}
		}
		if (workingImage == &converted) {
			image = std::move(converted);
		}
		return true;
	}

	// 法線を正規化し必要ならOpenGLのY成分をDirectX規約へ変換する
	bool NormalizeNormalMap(DirectX::ScratchImage& image, bool invertGreen) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		DirectX::ScratchImage normalized{};
		const HRESULT hr = DirectX::TransformImage(
			image.GetImages(), image.GetImageCount(), metadata,
			[invertGreen](DirectX::XMVECTOR* output, const DirectX::XMVECTOR* input,
				size_t width, size_t) {

				const DirectX::XMVECTOR one = DirectX::XMVectorReplicate(1.0f);
				const DirectX::XMVECTOR half = DirectX::XMVectorReplicate(0.5f);
				for (size_t x = 0; x < width; ++x) {

					DirectX::XMVECTOR normal = DirectX::XMVectorSubtract(
						DirectX::XMVectorScale(input[x], 2.0f), one);
					if (invertGreen) {
						normal = DirectX::XMVectorSetY(normal,
							-DirectX::XMVectorGetY(normal));
					}
					normal = DirectX::XMVector3Normalize(normal);
					DirectX::XMVECTOR encoded = DirectX::XMVectorMultiplyAdd(
						normal, half, half);
					output[x] = DirectX::XMVectorSetW(
						encoded, DirectX::XMVectorGetW(input[x]));
				}
			}, normalized);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(normalized);
		return true;
	}

	// 2D画像へフルMipチェーンを生成する
	bool GenerateMipChain(DirectX::ScratchImage& image) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
			metadata.mipLevels != 1 ||
			(metadata.width == 1 && metadata.height == 1)) {
			return false;
		}

		DirectX::ScratchImage mipChain{};
		DirectX::TEX_FILTER_FLAGS filter = DirectX::TEX_FILTER_DEFAULT;
		if (DirectX::IsSRGB(metadata.format)) {
			filter = static_cast<DirectX::TEX_FILTER_FLAGS>(
				filter | DirectX::TEX_FILTER_SRGB);
		}
		const HRESULT hr = DirectX::GenerateMipMaps(
			image.GetImages(), image.GetImageCount(), metadata,
			filter, 0, mipChain);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(mipChain);
		return true;
	}

	// インスペクター向けに指定チャンネルだけを可視化する
	bool ApplyPreviewChannel(DirectX::ScratchImage& image,
		Engine::TexturePreviewChannel channel) {

		if (channel == Engine::TexturePreviewChannel::Color) {
			return true;
		}

		DirectX::ScratchImage preview{};
		const HRESULT hr = DirectX::TransformImage(
			image.GetImages(), image.GetImageCount(), image.GetMetadata(),
			[channel](DirectX::XMVECTOR* output, const DirectX::XMVECTOR* input,
				size_t width, size_t) {

				for (size_t x = 0; x < width; ++x) {

					float value = 0.0f;
					switch (channel) {
					case Engine::TexturePreviewChannel::Red:
						value = DirectX::XMVectorGetX(input[x]);
						break;
					case Engine::TexturePreviewChannel::Green:
						value = DirectX::XMVectorGetY(input[x]);
						break;
					case Engine::TexturePreviewChannel::Blue:
						value = DirectX::XMVectorGetZ(input[x]);
						break;
					case Engine::TexturePreviewChannel::Alpha:
						value = DirectX::XMVectorGetW(input[x]);
						break;
					case Engine::TexturePreviewChannel::Normal:
						output[x] = DirectX::XMVectorSetW(input[x], 1.0f);
						continue;
					case Engine::TexturePreviewChannel::Color:
					default:
						output[x] = input[x];
						continue;
					}
					output[x] = DirectX::XMVectorSet(value, value, value, 1.0f);
				}
			}, preview);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(preview);
		return true;
	}

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

void Engine::TextureUploadService::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor, ID3D12CommandQueue* graphicsQueue) {

	Finalize();

	device_ = device;
	graphicsQueue_ = graphicsQueue;
	srvDescriptor_ = srvDescriptor;

	uploadCommand_ = std::make_unique<DxUploadCommand>();
	uploadCommand_->Create(device_);

	const uint32_t hardwareThreadCount = (std::max)(1u, std::thread::hardware_concurrency());
	const uint32_t threadCount = (std::min)(kMaxDecodeWorkerCount, hardwareThreadCount);
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

			uploaded = UploadSolidColor1x1(job.solidRGBA[0], job.solidRGBA[1], job.solidRGBA[2], job.solidRGBA[3]);

		}
		// 画像テクスチャのアップロード
		else if (job.success) {
			uploaded = UploadScratchImage(job.image, job.metadata, reuseSrvIndex);
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
			Logger::Output(LogType::Engine, "[TextureLoad][GPU] key={} srvIndex={} size={}x{} format={} status=READY",
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
			"[TextureReload][Deferred] key={} path={}", desc.key, desc.assetPath);
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
	Logger::Output(LogType::Engine, "[TextureReload][Queue] key={} path={}", desc.key, desc.assetPath);
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
		Logger::Output(LogType::Engine, "[TextureReload][Queue] key={} path={}", desc.key, desc.assetPath);
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

	uploadCommand_.reset();
	srvDescriptor_ = nullptr;
	graphicsQueue_ = nullptr;
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
	result.reload = job.reload;

	// ファイルパスからテクスチャをデコードする
	const std::filesystem::path fullPath = RuntimePaths::ResolveAssetPath(job.assetPath);
	const std::string extension = Algorithm::ToLower(
		Algorithm::PathToUTF8(fullPath.extension()));
	const std::wstring fullPathW = fullPath.wstring();
	const TextureColorSpace colorSpace = job.overrideImportColorSpace &&
		job.requestedColorSpace != TextureColorSpace::Auto ?
		job.requestedColorSpace : ResolveTextureColorSpace(
			job.importSettings, job.requestedColorSpace);

	DirectX::ScratchImage loaded{};
	HRESULT hr = E_FAIL;
	const char* failureStage = "Decode";
	DirectX::TexMetadata loadedMeta{};
	if (extension == ".dds") {

		hr = DirectX::LoadFromDDSFile(fullPathW.c_str(), DirectX::DDS_FLAGS_NONE, &loadedMeta, loaded);
	} else if (extension == ".tga") {

		hr = DirectX::LoadFromTGAFile(fullPathW.c_str(), &loadedMeta, loaded);
	} else if (extension == ".hdr") {

		hr = DirectX::LoadFromHDRFile(fullPathW.c_str(), &loadedMeta, loaded);
	} else {

		hr = DirectX::LoadFromWICFile(fullPathW.c_str(), ResolveWICFlags(colorSpace),
			&loadedMeta, loaded);
	}
	if (SUCCEEDED(hr)) {

		OverrideColorSpace(loaded, colorSpace);
		bool processingSucceeded = true;
		const bool isNormalMap = job.importSettings.preset == TextureImportPreset::NormalMap;
		if (isNormalMap) {

			failureStage = "NormalConvert";
			if (loaded.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {

				DirectX::ScratchImage linearImage{};
				hr = DirectX::Convert(loaded.GetImages(), loaded.GetImageCount(), loaded.GetMetadata(),
					DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
					DirectX::TEX_THRESHOLD_DEFAULT, linearImage);
				if (SUCCEEDED(hr)) {
					loaded = std::move(linearImage);
				} else {
					processingSucceeded = false;
				}
			}
			if (processingSucceeded) {

				failureStage = "NormalNormalize";
				processingSucceeded = NormalizeNormalMap(loaded,
					job.importSettings.normalConvention == TextureNormalConvention::OpenGL);
			}
		} else if (job.importSettings.alphaColorBleed &&
			(job.importSettings.preset == TextureImportPreset::Color ||
				job.importSettings.preset == TextureImportPreset::UI)) {

			BleedTransparentPixels(loaded);
		}

		const DirectX::TexMetadata& metadata = loaded.GetMetadata();
		const bool authoredDDSMips = extension == ".dds" && 1 < metadata.mipLevels;
		const bool needsMipChain = job.importSettings.generateMipmaps &&
			!authoredDDSMips && metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D &&
			metadata.mipLevels == 1 && (1 < metadata.width || 1 < metadata.height);
		if (processingSucceeded && needsMipChain) {

			failureStage = "MipGeneration";
			processingSucceeded = GenerateMipChain(loaded);
		}
		if (processingSucceeded && isNormalMap) {

			// Mip補間後に各法線を再正規化する、Y反転は基底Mipで完了している
			failureStage = "NormalMipNormalize";
			processingSucceeded = NormalizeNormalMap(loaded, false);
		}
		if (processingSucceeded) {
			failureStage = "PreviewChannel";
			processingSucceeded = ApplyPreviewChannel(loaded, job.previewChannel);
		}

		if (processingSucceeded) {
			result.image = std::move(loaded);
			result.metadata = result.image.GetMetadata();
			result.success = true;
		} else {
			hr = E_FAIL;
		}
	}
	const auto finishStats = decodeWorkers_.GetStats();
	uint32_t remainingProcessing = (0 < finishStats.inFlightCount) ? (finishStats.inFlightCount - 1) : 0;

	if (result.success) {

		Logger::Output(LogType::Engine,
			"[TextureLoad][Finish] Worker[{}/{}] processing={} queued={} "
			"key={} size={}x{} mips={} format={} status=OK",
			workerIndex + 1, finishStats.threadCount, remainingProcessing, finishStats.queuedCount, job.key,
			static_cast<uint32_t>(result.metadata.width), static_cast<uint32_t>(result.metadata.height),
			static_cast<uint32_t>(result.metadata.mipLevels), static_cast<uint32_t>(result.metadata.format));
	} else {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[TextureLoad][Finish] Worker[{}/{}] processing={} queued={} "
			"key={} status=FAILED stage={} hr=0x{:08X} path={}",
			workerIndex + 1, finishStats.threadCount, remainingProcessing,
			finishStats.queuedCount, job.key, failureStage,
			static_cast<uint32_t>(hr), job.assetPath);
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
	// COMMONにすることでグラフィクスキューへのクロスキュー受け渡しを正しく行う
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = result.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// コマンドを実行する
	uploadCommand_->ExecuteCommands(graphicsQueue_);

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
	const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta, uint32_t reuseSrvIndex) {

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
	UpdateSubresources(commandList, result.resource.Get(), uploadBuffer.Get(),
		0, 0, static_cast<UINT>(subResources.size()), subResources.data());

	// コピー後のリソースバリアを設定する
	// COMMONにすることでグラフィクスキューへのクロスキュー受け渡しを正しく行う
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = result.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// コマンドを実行する
	uploadCommand_->ExecuteCommands(graphicsQueue_);

	// SRVを作成する、reload時は既存indexへ上書きしてgpuHandleを変えない
	const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = BuildSRVDesc(meta);
	if (reuseSrvIndex != UINT32_MAX) {

		result.srvIndex = reuseSrvIndex;
		srvDescriptor_->RecreateSRV(reuseSrvIndex, result.resource.Get(), srvDesc);
	} else {

		srvDescriptor_->CreateSRV(result.srvIndex, result.resource.Get(), srvDesc);
	}
	result.gpuHandle = srvDescriptor_->GetGPUHandle(result.srvIndex);
	result.valid = true;
	return result;
}
