#include "TextureGPUUploader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

#include <vector>
#include <d3dx12.h>

namespace {

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

void Engine::TextureGPUUploader::Init(ID3D12Device* device, SRVDescriptor* descriptor, ID3D12CommandQueue* graphicsQueue) {

	device_ = device;
	srvDescriptor_ = descriptor;
	graphicsQueue_ = graphicsQueue;
	uploadCommand_ = std::make_unique<DxUploadCommand>();
	uploadCommand_->Create(device_);
}

void Engine::TextureGPUUploader::Finalize() {

	uploadCommand_.reset();
	srvDescriptor_ = nullptr;
	graphicsQueue_ = nullptr;
	device_ = nullptr;
}

Engine::GPUTextureResource Engine::TextureGPUUploader::UploadSolidColor1x1(
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

Engine::GPUTextureResource Engine::TextureGPUUploader::UploadScratchImage(
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
