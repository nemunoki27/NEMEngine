#include "GPUBufferLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

bool NEMTests::CheckMaskCompositeResampling(ID3D12Device* device, ID3D12CommandQueue* queue) {

	// 通常描画と同じShaderを使用
	Engine::DxShaderCompiler compiler;
	compiler.Init();
	const auto path =
		Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin/PostProcess/MaskComposite/postProcessMaskComposite.CS.hlsl");
	const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", L"main", Engine::ShaderStage::CS);
	if (!shader.IsValid()) {
		return false;
	}
	CD3DX12_DESCRIPTOR_RANGE ranges[2];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_ROOT_PARAMETER parameters[3];
	parameters[0].InitAsConstants(4, 1);
	parameters[1].InitAsDescriptorTable(1, &ranges[0]);
	parameters[2].InitAsDescriptorTable(1, &ranges[1]);
	const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(3, parameters, 1, &sampler);
	ComPtr<ID3DBlob> signature, errors;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
		FAILED(
			device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) {
		return false;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = {shader.GetBytecodePointer(), shader.GetBytecodeSize()};
	if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)))) {
		return false;
	}

	// 入力・出力・選別用Textureの解像度を分ける
	const auto inputDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 2, 1, 1);
	const auto flagsDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_UINT, 1, 1, 1, 1);
	const auto outputDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 4, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> input, flags, output;
	if (FAILED(device->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &inputDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&input))) ||
		FAILED(device->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &flagsDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&flags))) ||
		FAILED(device->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &outputDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) {
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT inputLayout{}, flagsLayout{}, outputLayout{};
	UINT64 inputBytes = 0, flagsBytes = 0, outputBytes = 0;
	device->GetCopyableFootprints(&inputDesc, 0, 1, 0, &inputLayout, nullptr, nullptr, &inputBytes);
	device->GetCopyableFootprints(&flagsDesc, 0, 1, 0, &flagsLayout, nullptr, nullptr, &flagsBytes);
	device->GetCopyableFootprints(&outputDesc, 0, 1, 0, &outputLayout, nullptr, nullptr, &outputBytes);
	ComPtr<ID3D12Resource> inputUpload, flagsUpload, readback;
	DxUtils::CreateUploadBufferResource(device, inputUpload, inputBytes);
	DxUtils::CreateUploadBufferResource(device, flagsUpload, flagsBytes);
	DxUtils::CreateReadbackBufferResource(device, readback, outputBytes);
	const std::array<std::array<float, 4>, 4> pixels{{{1, 0, 0, 0.25f}, {0, 1, 0, 0.5f}, {0, 0, 1, 0.75f}, {1, 1, 1, 1}}};
	void* mapped = nullptr;
	const D3D12_RANGE empty{0, 0};
	if (FAILED(inputUpload->Map(0, &empty, &mapped))) {
		return false;
	}
	for (size_t row = 0; row < 2; ++row) {
		std::memcpy(static_cast<std::byte*>(mapped) + row * inputLayout.Footprint.RowPitch, pixels.data() + row * 2,
			sizeof(pixels[0]) * 2);
	}
	inputUpload->Unmap(0, nullptr);
	if (FAILED(flagsUpload->Map(0, &empty, &mapped))) {
		return false;
	}
	std::memset(mapped, 0, static_cast<size_t>(flagsBytes));
	flagsUpload->Unmap(0, nullptr);

	// 同じ入力を置換合成し、αを保持して拡大
	ComPtr<ID3D12DescriptorHeap> descriptors;
	const D3D12_DESCRIPTOR_HEAP_DESC descriptorDesc{
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
	DxUtils::MakeDescriptorHeap(descriptors, device, descriptorDesc);
	const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = inputDesc.Format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(descriptors->GetCPUDescriptorHandleForHeapStart());
	for (UINT index = 0; index < 4; ++index) {
		srv.Format = index == 3 ? flagsDesc.Format : inputDesc.Format;
		device->CreateShaderResourceView(index == 3 ? flags.Get() : input.Get(), &srv, cpu);
		cpu.Offset(1, increment);
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
	uav.Format = outputDesc.Format;
	uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, cpu);
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Fence> fence;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
		return false;
	}
	const CD3DX12_TEXTURE_COPY_LOCATION inputTarget(input.Get(), 0), inputSource(inputUpload.Get(), inputLayout);
	const CD3DX12_TEXTURE_COPY_LOCATION flagsTarget(flags.Get(), 0), flagsSource(flagsUpload.Get(), flagsLayout);
	commands->CopyTextureRegion(&inputTarget, 0, 0, 0, &inputSource, nullptr);
	commands->CopyTextureRegion(&flagsTarget, 0, 0, 0, &flagsSource, nullptr);
	const D3D12_RESOURCE_BARRIER barriers[]{CD3DX12_RESOURCE_BARRIER::Transition(input.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
												D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(
			flags.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
	commands->ResourceBarrier(2, barriers);
	ID3D12DescriptorHeap* heaps[]{descriptors.Get()};
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootSignature(root.Get());
	const std::array<UINT, 4> constants{0, 4, 0, 0};
	commands->SetComputeRoot32BitConstants(0, 4, constants.data(), 0);
	commands->SetComputeRootDescriptorTable(1, descriptors->GetGPUDescriptorHandleForHeapStart());
	commands->SetComputeRootDescriptorTable(
		2, CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptors->GetGPUDescriptorHandleForHeapStart(), 4, increment));
	commands->Dispatch(1, 1, 1);
	const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
		output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commands->ResourceBarrier(1, &toRead);
	const CD3DX12_TEXTURE_COPY_LOCATION readTarget(readback.Get(), outputLayout), readSource(output.Get(), 0);
	commands->CopyTextureRegion(&readTarget, 0, 0, 0, &readSource, nullptr);
	if (FAILED(commands->Close())) {
		return false;
	}
	ID3D12CommandList* lists[]{commands.Get()};
	queue->ExecuteCommandLists(1, lists);
	if (FAILED(queue->Signal(fence.Get(), 1)) ||
		!Engine::DxDREDDiagnostics::WaitForFence(device, fence.Get(), 1, nullptr, "MaskCompositeTest")) {
		return false;
	}

	// 四隅と補間位置の色・αを読み戻す
	const D3D12_RANGE readRange{0, static_cast<size_t>(outputBytes)};
	if (FAILED(readback->Map(0, &readRange, &mapped))) {
		return false;
	}
	const std::array<std::array<UINT, 2>, 5> positions{{{0, 0}, {3, 0}, {0, 3}, {3, 3}, {1, 1}}};
	const std::array<std::array<float, 4>, 5> expected{
		{pixels[0], pixels[1], pixels[2], pixels[3], {0.625f, 0.25f, 0.25f, 0.4375f}}};
	bool valid = true;
	for (size_t index = 0; index < positions.size(); ++index) {
		std::array<float, 4> result{};
		std::memcpy(result.data(),
			static_cast<const std::byte*>(mapped) + positions[index][1] * outputLayout.Footprint.RowPitch +
				positions[index][0] * sizeof(result),
			sizeof(result));
		for (size_t channel = 0; channel < result.size(); ++channel) {
			valid &= std::isfinite(result[channel]) && std::abs(result[channel] - expected[index][channel]) < 0.00001f;
		}
	}
	readback->Unmap(0, &empty);
	if (!valid) {
		std::cerr << "MaskComposite resampling changed color or alpha\n";
	}
	return valid;
}
