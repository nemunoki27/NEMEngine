#include "DisplayOutputTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/DisplayOutputState.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandQueue.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Editor/UI/ImGui/ImGuiDisplayOutput.h>

// c++
#include <array>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <vector>

// directX
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>

using namespace Engine;

namespace {

	// Window切替の要求と適用結果を記録する
	struct WindowTestOutput {

		DisplayOutputMode mode = DisplayOutputMode::SDR;
		uint64_t revision = 1;
		DXGI_FORMAT applied = DXGI_FORMAT_UNKNOWN;
		bool rejectHDR = false;
	};

	// 実際のモニター能力とは独立してBackendの再構成を確認する
	bool ConfigureWindow(ImGui_ImplDX12_InitInfo* info, [[maybe_unused]] ImGuiViewport* viewport,
		IDXGISwapChain3* swapChain, ImGui_ImplDX12_OutputConfig* config, int operation) {

		auto& output = *static_cast<WindowTestOutput*>(info->OutputUserData);
		if (operation == 1 && output.rejectHDR && output.mode != DisplayOutputMode::SDR) {
			output.mode = DisplayOutputMode::SDR;
			++output.revision;
			const auto format = DisplayOutputState::ResolveFormat(output.mode);
			config->SwapChainFormat = format.buffer;
			config->RTVFormat = config->UIFormat = format.target;
			config->Revision = output.revision;
			return false;
		}
		const auto format = DisplayOutputState::ResolveFormat(output.mode);
		config->SwapChainFormat = format.buffer;
		config->RTVFormat = format.target;
		config->UIFormat = output.mode == DisplayOutputMode::SDR ? format.target : DXGI_FORMAT_R16G16B16A16_FLOAT;
		config->Revision = output.revision;
		if (operation == 1) {
			DXGI_SWAP_CHAIN_DESC1 desc{};
			if (FAILED(swapChain->GetDesc1(&desc)) || desc.BufferCount != 2 || desc.Format != format.buffer) return false;
			output.applied = desc.Format;
		}
		return true;
	}

	// 同じContextで形式変更と実行フレーム数の変更を繰り返す
	bool CheckWindowSwitches(WindowTestOutput& output) {

		HWND hwnd = CreateWindowExW(0, L"STATIC", L"DisplayOutputTest", WS_OVERLAPPEDWINDOW,
			0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!hwnd) return false;
		ImGuiViewport viewport;
		viewport.ID = 0x484452;
		viewport.Size = ImVec2(64, 64);
		viewport.PlatformHandleRaw = hwnd;
		auto& platform = ImGui::GetPlatformIO();
		platform.Renderer_CreateWindow(&viewport);
		platform.Viewports.push_back(&viewport);
		bool valid = true;
		for (uint32_t iteration = 0; iteration < 12; ++iteration) {
			for (const auto mode : { DisplayOutputMode::HDR10, DisplayOutputMode::ScRGB, DisplayOutputMode::SDR }) {
				output.mode = mode;
				++output.revision;
				ImGui_ImplDX12_SyncDisplayOutputs();
				valid &= output.applied == DisplayOutputState::ResolveFormat(mode).buffer;
				valid &= ImGui_ImplDX12_PrepareFrameCount((iteration % 2) + 1);
				ImGui_ImplDX12_ApplyFrameCount();
			}
		}
		output.rejectHDR = true;
		output.mode = DisplayOutputMode::HDR10;
		++output.revision;
		ImGui_ImplDX12_SyncDisplayOutputs();
		valid &= output.applied == DXGI_FORMAT_R8G8B8A8_UNORM;
		platform.Renderer_SetWindowSize(&viewport, ImVec2(80, 80));
		platform.Viewports.pop_back();
		platform.Renderer_DestroyWindow(&viewport);
		DestroyWindow(hwnd);
		return valid;
	}

	// UIとView画像の基準白を描画結果から比較する
	bool CheckComposition(ID3D12Device* device, DxCommandQueue& queue, RTVDescriptor& targets, SRVDescriptor& descriptors,
		DXGI_FORMAT format) {

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commands;
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)))) return false;
		RenderTexture2D source, target;
		RenderTextureCreateDesc desc{};
		desc.width = desc.height = 64;
		desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.clearColor = Color4(4.0f, 4.0f, 4.0f, 1.0f);
		source.Create(device, &targets, &descriptors, desc);
		desc.format = format;
		desc.clearColor = Color4::Black();
		target.Create(device, &targets, &descriptors, desc);
		const float bright[]{ 4.0f, 4.0f, 4.0f, 1.0f }, black[]{ 0.0f, 0.0f, 0.0f, 1.0f };
		commands->ClearRenderTargetView(source.GetRenderTarget().rtvHandle, bright, 0, nullptr);
		commands->ClearRenderTargetView(target.GetRenderTarget().rtvHandle, black, 0, nullptr);
		source.Transition(commands.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		ImGui_ImplDX12_NewFrame();
		auto& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(64, 64);
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(64, 64));
		ImGui::Begin("OutputTest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(ImVec2(0, 0), ImVec2(16, 16), IM_COL32_WHITE);
		ImGui::SetCursorScreenPos(ImVec2(16, 0));
		ImGuiDisplayOutput::DrawViewImage(static_cast<ImTextureID>(source.GetSRVGPUHandle().ptr), ImVec2(16, 16), 400.0f);
		draw->AddRectFilled(ImVec2(32, 0), ImVec2(48, 16), IM_COL32_WHITE);
		float white = 800.0f;
		draw->AddCallback([](const ImDrawList*, const ImDrawCmd* command) {
			ImGui_ImplDX12_SetImageWhiteLevel(*static_cast<const float*>(command->UserCallbackData));
		}, &white, sizeof(white));
		draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
		draw->AddImage(static_cast<ImTextureID>(source.GetSRVGPUHandle().ptr), ImVec2(0, 16), ImVec2(16, 32));
		draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
		ImGui::End();
		ImGui::Render();
		ImGui_ImplDX12_OutputConfig output{};
		output.UIFormat = format;
		output.UIWhiteScale = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 2.5f : 1.0f;
		const auto targetHandle = target.GetRenderTarget().rtvHandle;
		commands->OMSetRenderTargets(1, &targetHandle, FALSE, nullptr);
		ID3D12DescriptorHeap* heaps[]{ descriptors.GetDescriptorHeap() };
		commands->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.Get(), &output);

		// 実際に描かれた半精度画像を読み戻す
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64 bytes = 0;
		const auto textureDesc = target.GetResource()->GetDesc();
		device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
		ComPtr<ID3D12Resource> readback;
		DxUtils::CreateReadbackBufferResource(device, readback, bytes);
		target.Transition(commands.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		const CD3DX12_TEXTURE_COPY_LOCATION from(target.GetResource(), 0), to(readback.Get(), footprint);
		commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
		if (FAILED(commands->Close())) return false;
		ID3D12CommandList* lists[]{ commands.Get() };
		queue.GetQueue()->ExecuteCommandLists(1, lists);
		queue.SignalAndWait();
		void* mapped = nullptr;
		const D3D12_RANGE range{ 0, static_cast<SIZE_T>(bytes) };
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		bool valid = true;
		const std::array<std::array<uint32_t, 2>, 4> points{ { {8, 8}, {24, 8}, {40, 8}, {8, 24} } };
		const std::array expected{ 2.5f, 20.0f, 2.5f, 40.0f };
		for (size_t index = 0; index < points.size(); ++index) {
			const auto* row = static_cast<const std::byte*>(mapped) + points[index][1] * footprint.Footprint.RowPitch;
			const float value = format == DXGI_FORMAT_R16G16B16A16_FLOAT ?
				DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(row)[points[index][0] * 4]) :
				static_cast<float>(reinterpret_cast<const uint8_t*>(row)[points[index][0] * 4]) / 255.0f;
			const float wanted = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? expected[index] : 1.0f;
			if (std::abs(value - wanted) > 0.01f) {
				std::cerr << "ImGui output sample " << index << ": " << value << " expected " << wanted << '\n';
				valid = false;
			}
		}
		readback->Unmap(0, nullptr);
		return valid;
	}
}

bool NEMTests::TestDisplayOutputImGuiGPU() {

	ComPtr<ID3D12Debug> debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
	ComPtr<ID3D12Device> device;
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) return false;
	DxCommandQueue queue;
	queue.Create(device.Get());
	GraphicsResourceRetirement retirement;
	SRVDescriptor descriptors;
	RTVDescriptor targets;
	descriptors.Init(device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	targets.Init(device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE });
	descriptors.SetRetirementQueue(retirement);
	targets.SetRetirementQueue(retirement);
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	WindowTestOutput output;
	ImGui_ImplDX12_InitInfo info{};
	info.Device = device.Get();
	info.CommandQueue = queue.GetQueue();
	info.NumFramesInFlight = 2;
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	info.SrvDescriptorHeap = descriptors.GetDescriptorHeap();
	info.UserData = &descriptors;
	info.OutputUserData = &output;
	info.WindowOutputFn = ConfigureWindow;
	info.SrvDescriptorAllocFn = [](auto* init, auto* cpu, auto* gpu) {
		auto& heap = *static_cast<SRVDescriptor*>(init->UserData);
		const auto index = heap.Allocate();
		*cpu = heap.GetCPUHandle(index);
		*gpu = heap.GetGPUHandle(index);
	};
	info.SrvDescriptorFreeFn = [](auto* init, auto cpu, auto) {
		auto& heap = *static_cast<SRVDescriptor*>(init->UserData);
		const auto stride = init->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		heap.Retire(static_cast<uint32_t>((cpu.ptr - heap.GetCPUHandle(0).ptr) / stride), {});
	};
	bool valid = ImGui_ImplDX12_Init(&info) && ImGui_ImplDX12_CreateDeviceObjects();
	if (valid) {
		valid = CheckComposition(device.Get(), queue, targets, descriptors, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
			CheckComposition(device.Get(), queue, targets, descriptors, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
			CheckWindowSwitches(output);
	}
	queue.SignalAndWait();
	ImGui_ImplDX12_Shutdown();
	ImGui::DestroyContext();
	retirement.Seal(queue.GetLastSignaledFenceValue());
	retirement.Collect(queue.GetCompletedFenceValue());
	valid &= descriptors.GetUseDescriptorCount() == 0 && targets.GetUseDescriptorCount() == 0;
	ComPtr<ID3D12InfoQueue> diagnostics;
	if (SUCCEEDED(device.As(&diagnostics))) {
		for (UINT64 index = 0; index < diagnostics->GetNumStoredMessages(); ++index) {
			SIZE_T bytes = 0;
			diagnostics->GetMessage(index, nullptr, &bytes);
			std::vector<std::byte> storage(bytes);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			if (SUCCEEDED(diagnostics->GetMessage(index, message, &bytes)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
				std::cerr << message->pDescription << '\n';
				valid = false;
			}
		}
	}
	return valid;
}
