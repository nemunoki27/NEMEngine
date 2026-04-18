#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/Stage/AutoRootSignatureBuilder.h>
#include <Engine/Debug/Assert.h>

//============================================================================
//	RootBindingCommandHelper namespace
//============================================================================
namespace Engine::RootBindingCommand {

	//============================================================================
	//	グラフィックスコマンド
	//============================================================================

	// ルート引数にCBVをセットする
	void SetGraphicsCBV(ID3D12GraphicsCommandList* commandList, const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
	// ルート引数にSRVをセットする
	void SetGraphicsSRV(ID3D12GraphicsCommandList* commandList, const RootBindingLocation* binding,
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle = {});

	//============================================================================
	//	コンピュートコマンド
	//============================================================================

	// ルート引数にCBVをセットする
	void SetComputeCBV(ID3D12GraphicsCommandList* commandList, const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
	// ルート引数にSRVをセットする
	void SetComputeSRV(ID3D12GraphicsCommandList* commandList, const RootBindingLocation* binding,
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle = {});
	// ルート引数にUAVをセットする
	void SetComputeUAV(ID3D12GraphicsCommandList* commandList, const RootBindingLocation* binding,
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle = {});
}