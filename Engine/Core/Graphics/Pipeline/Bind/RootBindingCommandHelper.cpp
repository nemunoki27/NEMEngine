#include "RootBindingCommandHelper.h"

//============================================================================
//	RootBindingCommandHelper classMethods
//============================================================================

void Engine::RootBindingCommand::SetGraphicsCBV(ID3D12GraphicsCommandList* commandList,
	const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {

	if (binding->parameterType == D3D12_ROOT_PARAMETER_TYPE_CBV) {

		commandList->SetGraphicsRootConstantBufferView(binding->rootParameterIndex, gpuAddress);
		return;
	}
	Assert::Call(false, "Binding is not CBV root parameter");
}

void Engine::RootBindingCommand::SetGraphicsSRV(ID3D12GraphicsCommandList* commandList,
	const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle) {

	// SRVはGPUアドレスかテーブルハンドルのどちらかでセットする
	switch (binding->parameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_SRV:

		commandList->SetGraphicsRootShaderResourceView(binding->rootParameterIndex, gpuAddress);
		return;
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:

		commandList->SetGraphicsRootDescriptorTable(binding->rootParameterIndex, descriptorHandle);
		return;
	default:
		Assert::Call(false, "Binding is not SRV parameter");
	}
}

void Engine::RootBindingCommand::SetComputeCBV(ID3D12GraphicsCommandList* commandList,
	const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {

	if (binding->parameterType == D3D12_ROOT_PARAMETER_TYPE_CBV) {

		commandList->SetComputeRootConstantBufferView(binding->rootParameterIndex, gpuAddress);
		return;
	}
	Assert::Call(false, "Binding is not CBV root parameter");
}

void Engine::RootBindingCommand::SetComputeSRV(ID3D12GraphicsCommandList* commandList,
	const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle) {

	// SRVはGPUアドレスかテーブルハンドルのどちらかでセットする
	switch (binding->parameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_SRV:

		commandList->SetComputeRootShaderResourceView(binding->rootParameterIndex, gpuAddress);
		return;
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:

		commandList->SetComputeRootDescriptorTable(binding->rootParameterIndex, descriptorHandle);
		return;
	default:
		Assert::Call(false, "Binding is not SRV parameter");
	}
}

void Engine::RootBindingCommand::SetComputeUAV(ID3D12GraphicsCommandList* commandList,
	const RootBindingLocation* binding, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle) {

	// UAVはGPUアドレスかテーブルハンドルのどちらかでセットする
	switch (binding->parameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_UAV:

		commandList->SetComputeRootUnorderedAccessView(binding->rootParameterIndex, gpuAddress);
		return;
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:

		commandList->SetComputeRootDescriptorTable(binding->rootParameterIndex, descriptorHandle);
		return;
	default:
		Assert::Call(false, "Binding is not UAV parameter");
		return;
	}
}