#include "ImGuiTestFixture.h"
#include "TestImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiAppearance.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <cstring>
#include <stdexcept>
// imgui
#include <imgui_impl_dx12.h>
// directX
#include <DirectXTex.h>

using namespace Engine;
using namespace NEMTests;

ImGuiTestFixture::ImGuiTestFixture() {

	previous_ = ImGui::GetCurrentContext();
	context_ = ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1600, 1000);
	io.DeltaTime = 1.0f / 60.0f;
	ImGuiAppearance::ConfigureFont(io);
	ImGuiAppearance::ApplyTheme(ImGui::GetStyle(), false);
	targets_.Init(gpu_.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
	targets_.SetRetirementQueue(gpu_.retirement);
	output_.Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
		{.width = 1600, .height = 1000, .format = DXGI_FORMAT_R8G8B8A8_UNORM});
	ImGui_ImplDX12_InitInfo info{};
	info.Device = gpu_.device.Get();
	info.CommandQueue = gpu_.queue.GetQueue();
	info.NumFramesInFlight = 2;
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.SrvDescriptorHeap = gpu_.descriptors.GetDescriptorHeap();
	info.UserData = &gpu_.descriptors;
	info.SrvDescriptorAllocFn = [](auto* init, auto* cpu, auto* gpu) {
		auto& descriptors = *static_cast<SRVDescriptor*>(init->UserData);
		const auto index = descriptors.Allocate();
		*cpu = descriptors.GetCPUHandle(index);
		*gpu = descriptors.GetGPUHandle(index);
	};
	info.SrvDescriptorFreeFn = [](auto* init, auto cpu, auto) {
		auto& descriptors = *static_cast<SRVDescriptor*>(init->UserData);
		const auto stride = init->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		descriptors.Retire(static_cast<uint32_t>((cpu.ptr - descriptors.GetCPUHandle(0).ptr) / stride), {});
	};
	if (!ImGui_ImplDX12_Init(&info)) throw std::runtime_error("検証用UIの初期化に失敗しました");
}

ImGuiTestFixture::~ImGuiTestFixture() {

	gpu_.Finish();
	ImGui_ImplDX12_Shutdown();
	output_.Destroy();
	gpu_.Finish();
	ImGui::DestroyContext(context_);
	ImGui::SetCurrentContext(previous_);
}

void ImGuiTestFixture::Begin() {

	ImGui_ImplDX12_NewFrame();
	ImGui::NewFrame();
}

bool ImGuiTestFixture::End(const std::filesystem::path& imagePath) {

	ImGui::Render();
	output_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto* commands = gpu_.commands.GetCommandList();
	const auto target = output_.GetRenderTarget().rtvHandle;
	const float clear[]{0, 0, 0, 1};
	commands->ClearRenderTargetView(target, clear, 0, nullptr);
	commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands);
	if (!imagePath.empty()) return SaveImage(imagePath);
	gpu_.Finish();
	return true;
}

bool ImGuiTestFixture::SaveImage(const std::filesystem::path& path) {

	output_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	const auto desc = output_.GetResource()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT64 bytes = 0;
	gpu_.device.Get()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
	ComPtr<ID3D12Resource> readback;
	DxUtils::CreateReadbackBufferResource(gpu_.device.Get(), readback, bytes);
	const CD3DX12_TEXTURE_COPY_LOCATION from(output_.GetResource(), 0), to(readback.Get(), footprint);
	gpu_.commands.GetCommandList()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
	gpu_.Finish();
	void* mapped = nullptr;
	const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
	if (FAILED(readback->Map(0, &range, &mapped))) return false;
	const ScopedCleanup unmap([&]() noexcept { const D3D12_RANGE empty{}; readback->Unmap(0, &empty); });
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(desc.Format, desc.Width, desc.Height, 1, 1))) return false;
	for (uint32_t row = 0; row < desc.Height; ++row) {
		std::memcpy(image.GetPixels() + row * image.GetImage(0, 0, 0)->rowPitch,
			static_cast<uint8_t*>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
			static_cast<size_t>(desc.Width) * 4);
	}
	return SaveTestImage(*image.GetImage(0, 0, 0), path);
}

bool ImGuiTestFixture::CheckMessages() {

	return gpu_.CheckMessages();
}
