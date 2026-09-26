#include "GPUPipelineRetirementTests.h"

#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Core/DxUploadContext.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineBuilder.h>

#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>

// c++
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>
// directX
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <Externals/DirectX12/d3dx12.h>

#pragma comment(lib, "d3dcompiler.lib")

bool NEMTests::RecordPipelineOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
	Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback) {

	auto& retirement = descriptors.GetRetirementQueue();

	const char shader[] = "cbuffer Params : register(b0) { uint value; }; "
		"RWStructuredBuffer<uint> result : register(u0); [numthreads(1,1,1)] void main() { result[0] = value; }";
	ComPtr<ID3DBlob> code, errors, signature;
	if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "main", "cs_5_0",
		0, 0, &code, &errors))) return false;
	CD3DX12_ROOT_PARAMETER parameters[2];
	parameters[0].InitAsConstantBufferView(0);
	parameters[1].InitAsUnorderedAccessView(0);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, parameters);
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) return false;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&root)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = { code->GetBufferPointer(), code->GetBufferSize() };
	if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)))) return false;
	Engine::ViewConstantBuffer<uint32_t> constants;
	constants.Init(retirement, device);
	Engine::StructuredRWBuffer<uint32_t> output;
	output.Init(device, &descriptors);
	DxUtils::CreateReadbackBufferResource(device, readback, 12);
	commands->SetComputeRootSignature(root.Get());
	commands->SetPipelineState(pipeline.Get());
	// 同じframeに転送した3組の定数を別々に読み戻す
	const uint32_t values[]{ 73, 79, 83 };
	for (uint32_t index = 0; index < 3; ++index) {
		// 容量変更前のUAVも提出完了まで残す
		output.EnsureCapacity(index * 64 + 1);
		const auto toUAV = CD3DX12_RESOURCE_BARRIER::Transition(output.GetResource(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commands->ResourceBarrier(1, &toUAV);
		constants.Upload(values[index]);
		commands->SetComputeRootConstantBufferView(0, constants.GetGPUAddress());
		commands->SetComputeRootUnorderedAccessView(1, output.GetGPUAddress());
		commands->Dispatch(1, 1, 1);
		const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(output.GetResource(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands->ResourceBarrier(1, &toCopy);
		commands->CopyBufferRegion(readback.Get(), index * 4, output.GetResource(), 0, 4);
		const auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(output.GetResource(),
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commands->ResourceBarrier(1, &toWrite);
	}
	output.Release();
	retirement.Retire(std::move(pipeline));
	retirement.Retire(std::move(root));
	TestDirectory directory("PipelineRetirement");
	const auto shaderPath = directory.GetPath() / "retirement.cs.hlsl";
	{
		std::ofstream file(shaderPath);
		file << "[numthreads(1,1,1)] void main() {}";
		if (!file) return false;
	}
	Engine::DxShaderCompiler compiler;
	compiler.Init();
	ComPtr<ID3D12Device8> device8;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device8)))) return false;
	Engine::ComputePipelineDesc engineDesc{};
	engineDesc.compute = { .file = Engine::Algorithm::PathToUTF8(shaderPath), .entry = "main", .profile = "cs_6_0" };
	auto owned = Engine::PipelineStateBuilder::CreateCompute(retirement, device8.Get(), &compiler, engineDesc);
	std::filesystem::remove(shaderPath);
	if (!owned) return false;
	commands->SetComputeRootSignature(owned->GetRootSignature());
	commands->SetPipelineState(owned->GetComputePipeline());
	commands->Dispatch(1, 1, 1);
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support, sizeof(support)))) return false;
	if (support.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		const auto libraryPath = directory.GetPath() / "retirement.lib.hlsl";
		{
			std::ofstream file(libraryPath);
			file << "struct Payload { uint value; }; [shader(\"raygeneration\")] void RayGeneration() {} "
				"[shader(\"miss\")] void Miss(inout Payload payload) {}";
			if (!file) return false;
		}
		Engine::ShaderAsset shaderAsset{};
		for (const char* entry : { "RayGeneration", "Miss" }) {
			shaderAsset.stages.push_back({ .stage = Engine::ShaderStage::Lib,
				.file = Engine::Algorithm::PathToUTF8(libraryPath), .entry = entry, .profile = "lib_6_3" });
		}
		Engine::PipelineVariantDesc variant{};
		variant.kind = Engine::PipelineVariantKind::Raytracing;
		variant.rayGenerationExports = { "RayGeneration" };
		variant.missExports = { "Miss" };
		auto rayPipeline = Engine::RaytracingPipelineBuilder::Create(device8.Get(), &compiler, variant, shaderAsset);
		std::filesystem::remove(libraryPath);
		if (!rayPipeline) return false;
		rayPipeline->SetRetirementQueue(retirement);
		commands->SetComputeRootSignature(rayPipeline->GetRootSignature());
		commands->SetPipelineState1(rayPipeline->GetStateObject());
		const auto dispatch = rayPipeline->BuildDispatchDesc(1, 1);
		commands->DispatchRays(&dispatch);
	}
	return true;
}

bool NEMTests::RecordTexturePublication(ID3D12Device* device,
	ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback) {

	TestDirectory directory("TexturePublication");
	const auto path = directory.GetPath() / "reload.dds";
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1))) return false;
	auto save = [&](bool blue) {
		auto* pixel = image.GetPixels();
		pixel[0] = blue ? 0 : 255;
		pixel[1] = 0;
		pixel[2] = blue ? 255 : 0;
		pixel[3] = 255;
		return SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
			DirectX::DDS_FLAGS_NONE, path.c_str()));
	};
	Engine::TextureUploadService textures;
	textures.Init(device, &descriptors);
	Engine::TextureFileRequestDesc request;
	request.key = "TexturePublication";
	request.assetPath = Engine::Algorithm::PathToUTF8(path);
	request.importSettings.generateMipmaps = false;
	request.importSettings.alphaColorBleed = false;
	request.requestedColorSpace = Engine::TextureColorSpace::Linear;
	if (!save(false)) return false;
	textures.RequestTextureFile(request);
	textures.WaitAll();
	const auto* first = textures.GetTexture(request.key);
	if (!first || !first->valid) return false;
	const uint32_t firstIndex = first->srvIndex;
	const uint64_t firstRevision = textures.GetContentRevision();
	DxUtils::CreateReadbackBufferResource(device, readback, 1024);
	auto copy = [&](ID3D12Resource* resource, uint64_t offset) {
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource,
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = resource;
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION target{};
		target.pResource = readback.Get();
		target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		const auto desc = resource->GetDesc();
		device->GetCopyableFootprints(&desc, 0, 1, offset, &target.PlacedFootprint, nullptr, nullptr, nullptr);
		commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
	};
	// 未提出の旧Texture参照を残して再読み込みする
	copy(first->resource.Get(), 0);
	if (!save(true)) return false;
	textures.RequestReload(request.key);
	textures.WaitAll();
	const auto* second = textures.GetTexture(request.key);
	if (!second || !second->valid || second->srvIndex == firstIndex ||
		!descriptors.IsAllocated(firstIndex) || textures.GetContentRevision() != firstRevision + 1) return false;
	const uint32_t secondIndex = second->srvIndex;
	copy(second->resource.Get(), 512);
	// 失敗した再読み込みでは公開世代と番号を変えない
	std::filesystem::remove(path);
	textures.RequestReload(request.key);
	textures.WaitAll();
	const auto* retained = textures.GetTexture(request.key);
	const bool valid = retained && retained->srvIndex == secondIndex && textures.GetContentRevision() == firstRevision + 1;
	textures.Finalize();
	return valid && descriptors.IsAllocated(firstIndex) && descriptors.IsAllocated(secondIndex);
}

bool NEMTests::RecordImGuiRetirement(ID3D12Device* device, ID3D12CommandQueue* queue,
	ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors) {

	ImGui::CreateContext();
	struct ContextLifetime {
		bool backend = false;
		~ContextLifetime() {
			if (backend) ImGui_ImplDX12_Shutdown();
			ImGui::DestroyContext();
		}
	} lifetime;
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().DisplaySize = ImVec2(8.0f, 8.0f);
	ImGui_ImplDX12_InitInfo info{};
	info.Device = device;
	info.CommandQueue = queue;
	info.NumFramesInFlight = 3;
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.SrvDescriptorHeap = descriptors.GetDescriptorHeap();
	info.UserData = &descriptors;
	info.SrvDescriptorAllocFn = [](auto* init, auto* cpu, auto* gpu) {
		auto& heap = *static_cast<Engine::SRVDescriptor*>(init->UserData);
		const uint32_t index = heap.Allocate();
		*cpu = heap.GetCPUHandle(index);
		*gpu = heap.GetGPUHandle(index);
	};
	info.SrvDescriptorFreeFn = [](auto* init, auto cpu, auto) {
		auto& heap = *static_cast<Engine::SRVDescriptor*>(init->UserData);
		const uint32_t stride = init->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		heap.Retire(static_cast<uint32_t>((cpu.ptr - heap.GetCPUHandle(0).ptr) / stride), {});
	};
	info.WaitForGPUFn = [](auto* init, ID3D12Fence* fence, UINT64 value, HANDLE event) {
		return fence ? Engine::DxDredDiagnostics::WaitForFence(init->Device, fence, value, event, "ImGuiTest::Fence") :
			event ? Engine::DxDredDiagnostics::WaitForEvent(init->Device, event, "ImGuiTest::Present") :
			Engine::DxDredDiagnostics::CheckDeviceState(init->Device, "ImGuiTest::Device");
	};
	info.ResourceRetireFn = [](auto* init, ID3D12Object* resource) {
		auto& heap = *static_cast<Engine::SRVDescriptor*>(init->UserData);
		heap.GetRetirementQueue().Retire(ComPtr<ID3D12Object>(resource));
	};
	if (!ImGui_ImplDX12_Init(&info)) return false;
	lifetime.backend = true;
	ImGui_ImplDX12_NewFrame();
	ImGui::NewFrame();
	ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(8, 8), IM_COL32_WHITE);
	ImGui::Render();
	ComPtr<ID3D12Resource> target;
	const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	const auto description = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)))) return false;
	ComPtr<ID3D12DescriptorHeap> targets;
	D3D12_DESCRIPTOR_HEAP_DESC targetHeap{};
	targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	targetHeap.NumDescriptors = 1;
	if (FAILED(device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&targets)))) return false;
	const auto handle = targets->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(target.Get(), nullptr, handle);
	commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	ID3D12DescriptorHeap* heaps[]{ descriptors.GetDescriptorHeap() };
	commands->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands);
	const size_t beforeShutdown = descriptors.GetRetirementQueue().GetPendingCount();
	// 提出前にBackendを終了し、GPU資源だけを回収窓口へ残す
	ImGui_ImplDX12_Shutdown();
	lifetime.backend = false;
	const bool retained = descriptors.GetRetirementQueue().GetPendingCount() > beforeShutdown;
	descriptors.GetRetirementQueue().Retire(std::move(target));
	return retained;
}

bool NEMTests::CheckFenceWaitAndRemoval() {

	// テスト専用のWARPDeviceで消失を再現する
	ComPtr<IDXGIFactory4> factory;
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<ID3D12Device5> device;
	ComPtr<ID3D12Fence> fence;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
		FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
		FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))) ||
		FAILED(device->CreateFence(1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	if (!Engine::DxDredDiagnostics::WaitForFence(device.Get(), fence.Get(), 1, nullptr, "FenceTest::Completed")) return false;
	Engine::GraphicsResourceRetirement retirement;
	Engine::BaseDescriptor descriptors(2);
	descriptors.Init(device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	descriptors.SetRetirementQueue(retirement);
	descriptors.Retire(descriptors.Allocate(), {});
	retirement.Seal(2);
	descriptors.Retire(descriptors.Allocate(), {});
	bool rejected = false;
	try { retirement.ReleaseAfterDeviceRemoval(device.Get()); }
	catch (const std::logic_error&) { rejected = true; }
	if (!rejected || retirement.GetPendingCount() != 2) return false;
	Engine::DxCommand commands;
	commands.Create(device.Get());
	commands.CloseCommandList();
	Engine::DxUploadCommand uploads;
	uploads.Create(device.Get());
	uploads.ExecuteCommands({});
	HANDLE completion = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!completion) return false;
	const auto started = std::chrono::steady_clock::now();
	std::jthread removal([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		device->RemoveDevice();
	});
	const bool completed = Engine::DxDredDiagnostics::WaitForFence(device.Get(), fence.Get(), 2, completion, "FenceTest::Removed");
	CloseHandle(completion);
	removal.join();
	// 消失後のResetと転送を失敗として通知する
	bool resetRejected = false;
	try { commands.ResetCommandList(); }
	catch (const std::runtime_error&) { resetRejected = true; }
	bool uploadRejected = false;
	try { uploads.ExecuteCommands({}); }
	catch (const std::runtime_error&) { uploadRejected = true; }
	bool recordingRejected = false;
	try { uploads.GetCommandList(); }
	catch (const std::logic_error&) { recordingRejected = true; }
	const auto elapsed = std::chrono::steady_clock::now() - started;
	retirement.Collect(UINT64_MAX);
	const bool retained = retirement.GetPendingCount() == 2;
	retirement.ReleaseAfterDeviceRemoval(device.Get());
	Engine::DxDredDiagnostics::ResetForNewDevice();
	return !completed && retained && resetRejected && !commands.IsRecording() && uploadRejected && recordingRejected && elapsed < std::chrono::seconds(5) &&
		retirement.GetPendingCount() == 0 && descriptors.GetUseDescriptorCount() == 0;
}
