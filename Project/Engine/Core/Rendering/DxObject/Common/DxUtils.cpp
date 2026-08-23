#include "DxUtils.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

//============================================================================
//	DxUtils namespaceMethods
//============================================================================
void DxUtils::MakeDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc) {

	if (!device) {
		Engine::Assert::Call(false, "DescriptorHeap作成にDirectX 12デバイスが必要です");
		return;
	}
	HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap));
	Engine::Assert::Call(SUCCEEDED(hr), "DescriptorHeapの作成に失敗しました");
}

D3D12_RESOURCE_DESC DxUtils::MakeBufferResourceDesc(size_t sizeInBytes, D3D12_RESOURCE_FLAGS flags) {

	// バッファリソースの共通定義でテクスチャの場合はまた別の設定をする
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	// バッファの場合はこれらは1にする決まり
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = flags;
	return resourceDesc;
}

void DxUtils::CreateUploadBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	if (!device || sizeInBytes == 0) {
		Engine::Assert::Call(false, "UploadBuffer作成にはDeviceと1Byte以上のサイズが必要です");
		return;
	}

	// CPUから書き込むためUPLOAD heapに配置し、永続Mapを前提にGENERIC_READで作る
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC resourceDesc = MakeBufferResourceDesc(sizeInBytes);

	HRESULT hr = device->CreateCommittedResource(
		&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	Engine::DxDredDiagnostics::CheckHRESULT(
		device, hr, "DxUtils::CreateUploadBufferResource/CreateCommittedResource");
	Engine::Assert::Call(SUCCEEDED(hr), "UploadBufferの作成に失敗しました");
}

void DxUtils::CreateDefaultBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes,
	[[maybe_unused]] D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags) {

	if (!device || sizeInBytes == 0) {
		Engine::Assert::Call(false, "DefaultBuffer作成にはDeviceと1Byte以上のサイズが必要です");
		return;
	}

	// GPU専用のDEFAULT heap
	// D3D12 bufferはCreateCommittedResourceのInitialStateにCOPY_DEST等を指定してもCOMMONとして扱われる
	// Debug Layer #1328を避けるため作成時はCOMMON固定にし、必要な遷移は呼び出し側のCommandListで行う
	D3D12_HEAP_PROPERTIES defaultHeapProperties{};
	defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC resourceDesc = MakeBufferResourceDesc(sizeInBytes, flags);

	HRESULT hr = device->CreateCommittedResource(
		&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	Engine::Assert::Call(SUCCEEDED(hr), "DefaultBufferの作成に失敗しました");
}

void DxUtils::CreateBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	CreateUploadBufferResource(device, resource, sizeInBytes);
}

void DxUtils::CreateUavBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	if (!device || sizeInBytes == 0) {
		Engine::Assert::Call(false, "UAV Buffer作成にはDeviceと1Byte以上のサイズが必要です");
		return;
	}

	HRESULT hr;

	// リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes;
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	vertexResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	Engine::Assert::Call(SUCCEEDED(hr), "UAV対応Bufferの作成に失敗しました");
}

void DxUtils::CreateReadbackBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	if (!device || sizeInBytes == 0) {
		Engine::Assert::Call(false, "ReadbackBuffer作成にはDeviceと1Byte以上のサイズが必要です");
		return;
	}

	HRESULT hr;

	// リソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	// バッファリソースでテクスチャの場合はまた別の設定をする
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	// リソースのサイズ
	resourceDesc.Width = sizeInBytes;
	// バッファの場合はこれらは1にする決まり
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);

	// リソース作成
	hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	Engine::Assert::Call(SUCCEEDED(hr), "ReadbackBufferの作成に失敗しました");
}

bool DxUtils::CanAllocateIndex(uint32_t useIndex, uint32_t kMaxCount) {

	return useIndex < kMaxCount;
}

UINT DxUtils::RoundUp(UINT round, UINT thread) {

	if (thread == 0) {
		Engine::Assert::Call(false, "切り上げ除算の除数に0は指定できません");
		return 0;
	}
	return (round + thread - 1) / thread;
}
