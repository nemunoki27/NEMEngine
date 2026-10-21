#include "FrameBufferingTests.h"
#include "TestFixtures.h"
#include "TestRunner.h"
#include "GPUFilterTestFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/Core/GraphicsPreferenceStorage.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandQueue.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

// c++
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>

// directX
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

// imgui
#include <imgui.h>
#include <imgui_impl_dx12.h>

namespace {

	// 実際のユーザー設定に触れず保存と旧設定の移行を確認する
	bool CheckPreferences() {

		using namespace Engine;
		NEMTests::TestDirectory directory("FrameBufferingPreferences",
			RuntimePaths::GetEngineProjectRoot().parent_path() / "Generated/Refactoring/DoubleBuffering");
		struct SettingsScope {
			std::string previous;
			SettingsScope() {
				char* value = nullptr;
				size_t length = 0;
				if (_dupenv_s(&value, &length, "NEMENGINE_USER_SETTINGS_ROOT") == 0 && value) previous = value;
				std::free(value);
			}
			~SettingsScope() {
				_putenv_s("NEMENGINE_USER_SETTINGS_ROOT", previous.c_str());
				RuntimePaths::Refresh();
			}
		} scope;
		_putenv_s("NEMENGINE_USER_SETTINGS_ROOT", Algorithm::PathToUTF8(directory.GetPath()).c_str());
		RuntimePaths::Refresh();
		const auto path = RuntimePaths::GetUserSettingsPath(ConfigPaths::kGraphicsFeatureSettings);
		GraphicsFeaturePreferences preferences{};
		bool valid = preferences.frameContextCount == 2;
		GraphicsPreferenceStorage::Load(preferences);
		valid &= preferences.frameContextCount == 2;
		for (uint32_t count : { 1u, 2u }) {
			preferences.frameContextCount = count;
			GraphicsPreferenceStorage::Save(preferences);
			GraphicsFeaturePreferences restored{};
			GraphicsPreferenceStorage::Load(restored);
			valid &= restored.frameContextCount == count;
		}
		if (!JsonFile::Save(path, { { "frameContextCount", 3u }, { "allowMeshShader", false } })) return false;
		GraphicsPreferenceStorage::Load(preferences);
		return valid && preferences.frameContextCount == 2 && !preferences.allowMeshShader &&
			JsonFile::Load(path, false).value("frameContextCount", 0u) == 2;
	}

	// GPU資源の移動後も解放通知を受け取れるか調べる
	bool CheckSlotOwnership(ID3D12Device* device, Engine::GraphicsResourceRetirement& retirement) {

		Engine::DxFrameMappedUploadBuffer source;
		source.SetRetirementQueue(retirement);
		source.EnsureCapacity(device, 256, "FrameCountTest");
		auto* retained = source.GetResource(0);
		Engine::DxFrameMappedUploadBuffer moved(std::move(source));
		retirement.ReleaseInactiveFrameResources(1);
		if (source.GetResource(0) || moved.GetResource(1) || moved.GetResource(0) != retained) return false;
		moved.EnsureCapacity(device, 256, "FrameCountTest");
		return moved.GetResource(1) != nullptr;
	}

	// ImGuiのContextを維持して候補の取消と切替を繰り返す
	bool CheckImGuiFrames(ID3D12Device* device, ID3D12CommandQueue* queue, Engine::SRVDescriptor& descriptors,
		uint32_t initialCount) {

		ImGui::CreateContext();
		ImGui::GetIO().IniFilename = nullptr;
		ImGui_ImplDX12_InitInfo info{};
		info.Device = device;
		info.CommandQueue = queue;
		info.NumFramesInFlight = static_cast<int>(initialCount);
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
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
		const bool initialized = ImGui_ImplDX12_Init(&info);
		bool valid = initialized;
		if (initialized) {
			if (!ImGui_ImplDX12_CreateDeviceObjects()) {
				ImGui_ImplDX12_Shutdown();
				ImGui::DestroyContext();
				return false;
			}
			auto* context = ImGui::GetCurrentContext();
			// 外部Windowも表示用2枚と実行用1枚へ分離できるか調べる
			HWND window = CreateWindowExW(0, L"STATIC", L"FrameBufferingTest", WS_OVERLAPPEDWINDOW,
				0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			ImGuiViewport external;
			external.ID = 0xFB001;
			external.Size = ImVec2(64, 64);
			external.PlatformHandleRaw = window;
			auto& platform = ImGui::GetPlatformIO();
			if (window) {
				platform.Renderer_CreateWindow(&external);
				platform.Viewports.push_back(&external);
			} else { std::cerr << "CreateWindowExW failed: " << GetLastError() << '\n'; valid = false; }
			for (uint32_t iteration = 0; iteration < 100; ++iteration) {
				valid &= ImGui_ImplDX12_PrepareFrameCount(1);
				ImGui_ImplDX12_CancelFrameCount();
				for (int count : { 1, 2 }) {
					valid &= ImGui_ImplDX12_PrepareFrameCount(count);
					ImGui_ImplDX12_ApplyFrameCount();
					valid &= context == ImGui::GetCurrentContext();
				}
			}
			valid &= !ImGui_ImplDX12_PrepareFrameCount(3);
			if (window) {
				platform.Viewports.pop_back();
				platform.Renderer_DestroyWindow(&external);
				DestroyWindow(window);
			}
			ImGui_ImplDX12_Shutdown();
		}
		ImGui::DestroyContext();
		return valid;
	}

	// 分割転送の所有、予算、取消と転送内容を検証する
	bool CheckOwnedUploads(ID3D12Device* device, Engine::DxCommandQueue& queue, Engine::BufferUploadService& uploads) {

		using namespace Engine;
		constexpr size_t size = 40 * 1024 * 1024;
		auto values = std::make_shared<std::vector<uint32_t>>(size / 4);
		for (size_t index = 0; index < values->size(); ++index) (*values)[index] = uint32_t(index * 17 + 13);
		std::weak_ptr<const void> owner = values;
		ComPtr<ID3D12Resource> destination, readback;
		DxUtils::CreateDefaultBufferResource(device, destination, size);
		DxUtils::CreateReadbackBufferResource(device, readback, 12);
		GraphicsFrameState::BeginFrame(0);
		const auto ticket = uploads.BeginOwnedUploads(values);
		// 端数のある複数要求でもframeの転送予算を超えない
		const size_t split = 1024 * 1024 - 1;
		uploads.EnqueueBufferUpload(destination.Get(), std::as_bytes(std::span(*values).first(split)), D3D12_RESOURCE_STATE_COPY_SOURCE);
		uploads.EnqueueBufferUpload(destination.Get(), split * 4, std::as_bytes(std::span(*values).subspan(split)),
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
		uploads.FinishOwnedUploads();
		values.reset();
		if (owner.expired() || uploads.IsComplete(ticket)) return false;
		const auto before = uploads.GetStatistics().uploadedBytes;
		uploads.PumpOwnedUploads();
		if (uploads.GetStatistics().uploadedBytes - before > 32 * 1024 * 1024 || owner.expired()) return false;
		uploads.FlushAndWait();
		if (!uploads.IsComplete(ticket) || !owner.expired() || uploads.GetStatistics().pendingBytes != 0 ||
			uploads.GetStatistics().largePooledBytes > 64 * 1024 * 1024) return false;
		DxCommand commands;
		commands.Create(device);
		const std::array<size_t, 3> offsets{0, 32 * 1024 * 1024, size - 4};
		for (size_t index = 0; index < offsets.size(); ++index) {
			commands.GetCommandList()->CopyBufferRegion(readback.Get(), index * 4, destination.Get(), offsets[index], 4);
		}
		commands.CloseCommandList();
		queue.ExecuteCommandList(commands.GetCommandList());
		queue.SignalAndWait();
		void* mapped = nullptr;
		const D3D12_RANGE range{0, 12}, written{0, 0};
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		bool valid = true;
		for (size_t index = 0; index < offsets.size(); ++index) {
			valid &= static_cast<const uint32_t*>(mapped)[index] == uint32_t(offsets[index] / 4 * 17 + 13);
		}
		readback->Unmap(0, &written);
		values = std::make_shared<std::vector<uint32_t>>(size / 4, 77);
		owner = values;
		const auto canceled = uploads.BeginOwnedUploads(values);
		uploads.EnqueueBufferUpload(destination.Get(), std::as_bytes(std::span(*values)), D3D12_RESOURCE_STATE_COPY_SOURCE);
		uploads.FinishOwnedUploads();
		values.reset();
		uploads.CancelOwnedUploads(canceled);
		uploads.FlushAndWait();
		return valid && owner.expired() && !uploads.IsComplete(canceled) && uploads.GetStatistics().pendingBytes == 0;
	}

	// スロットを増減しながらGPUへ渡した内容とDescriptor数を比較する
	bool CheckRepeatedFrameChanges(ID3D12Device* device) {

		Engine::GraphicsResourceRetirement retirement;
		Engine::SRVDescriptor descriptors;
		descriptors.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
		descriptors.SetRetirementQueue(retirement);
		Engine::DxCommandQueue queue;
		queue.Create(device);
		if (!NEMTests::CheckFilterTransitionBatch(device, queue.GetQueue())) return false;
		Engine::BufferUploadService uploads;
		uploads.Init(retirement, device, queue.GetQueue());
		if (!NEMTests::RunTest("CheckOwnedUploads", [&] { return CheckOwnedUploads(device, queue, uploads); })) return false;
		Engine::DxCommand commands;
		commands.Create(device);
		commands.CloseCommandList();
		ComPtr<ID3D12Resource> readback;
		DxUtils::CreateReadbackBufferResource(device, readback, sizeof(uint32_t) * 4);
		bool valid = NEMTests::RunTest("CheckSlotOwnership", [&] { return CheckSlotOwnership(device, retirement); });
		queue.SignalAndWait();
		retirement.Seal(queue.GetLastSignaledFenceValue());
		retirement.Collect(queue.GetCompletedFenceValue());
		valid &= NEMTests::RunTest("CheckImGuiFrames", [&] { return CheckImGuiFrames(device, queue.GetQueue(), descriptors, 2); });
		valid &= NEMTests::RunTest("CheckSingleFrameImGuiStartup", [&] {
			return CheckImGuiFrames(device, queue.GetQueue(), descriptors, 1);
		});
		if (!valid) return false;
		{
			Engine::DefaultStructuredInstanceBuffer<uint32_t> defaults;
			Engine::StructuredInstanceBuffer<uint32_t> instances;
			defaults.Init(device, &descriptors, &uploads);
			instances.Init(device, &descriptors);
			size_t doubleDescriptors = 0;
			for (uint32_t iteration = 0; iteration < 100; ++iteration) {
				for (uint32_t count : { 2u, 1u }) {
					queue.SignalAndWait();
					auto allocator = commands.PrepareFrameContextCount(count);
					if (!retirement.GetFrameResources().Prepare(count)) return false;
					retirement.ReleaseInactiveFrameResources(count);
					commands.ApplyFrameContextCount(count, 0, std::move(allocator));
					for (uint32_t index = 0; index < count; ++index) {
						if (!queue.WaitForFenceValue(commands.GetFrameFenceValue(index), "FrameCountTest")) return false;
						commands.BeginFrame(index);
						const auto submission = commands.GetSubmissionTicket();
						if (!submission || submission->GetFenceValue() != 0) return false;
						const uint32_t value = 1000 * iteration + count * 10 + index;
						instances.Upload(std::span<const uint32_t>(&value, 1));
						defaults.MarkFullUpdate(1);
						defaults.UploadCurrentFrame(std::span<const uint32_t>(&value, 1));
						commands.GetCommandList()->CopyBufferRegion(readback.Get(), 0, instances.GetResource(), 0, sizeof(value));
						commands.GetCommandList()->CopyBufferRegion(readback.Get(), sizeof(value) * 2, defaults.GetResource(), 0, sizeof(value));
						const uint32_t nextValue = value + 1;
						// 同じframeの先行描画へ後の値を上書きしない
						instances.Upload(std::span<const uint32_t>(&nextValue, 1));
						defaults.MarkFullUpdate(1);
						defaults.UploadCurrentFrame(std::span<const uint32_t>(&nextValue, 1));
						commands.GetCommandList()->CopyBufferRegion(readback.Get(), sizeof(value), instances.GetResource(), 0, sizeof(value));
						commands.GetCommandList()->CopyBufferRegion(readback.Get(), sizeof(value) * 3, defaults.GetResource(), 0, sizeof(value));
						commands.CloseCommandList();
						uploads.SubmitBatch();
						queue.ExecuteCommandList(commands.GetCommandList());
						const auto fence = queue.Signal();
						commands.SetCurrentFrameFenceValue(fence);
						if (submission->GetFenceValue() != fence) return false;
						retirement.Seal(fence);
						if (!queue.WaitForFenceValue(fence, "FrameCountTest")) return false;
						retirement.Collect(queue.GetCompletedFenceValue());
						D3D12_RANGE range{ 0, sizeof(value) * 4 };
						void* mapped = nullptr;
						if (FAILED(readback->Map(0, &range, &mapped))) return false;
						const std::array<uint32_t, 4> expected{ value, nextValue, value, nextValue };
						const bool matches = std::memcmp(mapped, expected.data(), sizeof(expected)) == 0;
						if (!matches && iteration == 0) {
							const auto* actual = static_cast<const uint32_t*>(mapped);
							std::cerr << "Frame data mismatch: " << count << '/' << index << " actual="
								<< actual[0] << ',' << actual[1] << ',' << actual[2] << ',' << actual[3]
								<< " expected=" << value << ',' << nextValue << '\n';
						}
						valid &= matches;
						const D3D12_RANGE written{ 0, 0 };
						readback->Unmap(0, &written);
					}
					const size_t allocated = descriptors.GetUseDescriptorCount();
					if (count == 2) {
						if (iteration == 0) doubleDescriptors = allocated;
						if (allocated != doubleDescriptors) {
							std::cerr << "Double frame descriptors: " << allocated << " expected=" << doubleDescriptors << '\n';
						}
						valid &= allocated == doubleDescriptors;
					} else {
						if (allocated * 2 != doubleDescriptors) {
							std::cerr << "Single frame descriptors: " << allocated << " expected=" << doubleDescriptors / 2 << '\n';
						}
						valid &= allocated * 2 == doubleDescriptors;
					}
					valid &= Engine::GraphicsFrameState::GetActiveCount() == count;
				}
			}
		}
		uploads.FlushAndWait();
		uploads.Finalize();
		queue.SignalAndWait();
		retirement.Seal(queue.GetLastSignaledFenceValue());
		retirement.Collect(queue.GetCompletedFenceValue());
		if (descriptors.GetUseDescriptorCount() || retirement.GetPendingCount()) {
			std::cerr << "Frame resources retained: descriptors=" << descriptors.GetUseDescriptorCount()
				<< " pending=" << retirement.GetPendingCount() << '\n';
		}
		return valid && descriptors.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
	}
}

bool NEMTests::TestFrameBufferingContracts() {

	// 任意Shaderと切り抜きは不透明確定の対象にしない
	auto material = Engine::CreateDefaultMeshMaterialAsset("PolicyTest");
	if (!Engine::MaterialRenderPolicy::IsOpaqueGeometry(&material, Engine::MaterialSurfaceMode::Opaque) ||
		Engine::MaterialRenderPolicy::IsOpaqueGeometry(&material, Engine::MaterialSurfaceMode::Masked) ||
		Engine::MaterialRenderPolicy::IsOpaqueGeometry(nullptr, Engine::MaterialSurfaceMode::Opaque)) return false;
	material.shaderGraph = Engine::AssetID::New();
	if (Engine::MaterialRenderPolicy::IsStandardSurface(material)) return false;
	// 配布している既定AssetのPass構成も判定対象に含める
	if (!Engine::FromJson(Engine::JsonFile::Load(Engine::RuntimePaths::GetEngineAssetPath(
		"Shaders/Builtin/Mesh/MeshPBR/meshPBR.material.json"), false), material) ||
		!Engine::MaterialRenderPolicy::IsStandardSurface(material)) return false;
	const auto compact = Engine::RenderPathResources::BuildSceneMainDesc(17, 13, DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (compact.colors[0].format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
		compact.colors[2].format != DXGI_FORMAT_R32G32B32A32_FLOAT ||
		Engine::RenderPathResources::BuildSceneFinalDesc(17, 13).colors[0].format != DXGI_FORMAT_R32G32B32A32_FLOAT) return false;
	Engine::GraphicsResourceRetirement retirement;
	Engine::GraphicsFrameResourceArray<std::unique_ptr<int>> original;
	original.Connect(retirement);
	original[0] = std::make_unique<int>(11);
	original[1] = std::make_unique<int>(22);
	auto moved = std::move(original);
	retirement.ReleaseInactiveFrameResources(1);
	bool valid = moved[0] && *moved[0] == 11 && !moved[1];
	bool cancelled = false;
	Engine::GraphicsFrameResourceRegistration failing;
	failing.Bind(retirement.GetFrameResources(), &cancelled, [](void*, uint32_t) {},
		[](void*, uint32_t) { return false; }, [](void* owner, uint32_t) { *static_cast<bool*>(owner) = true; });
	valid &= !retirement.GetFrameResources().Prepare(2) && cancelled && moved[0] && *moved[0] == 11;
	failing.Reset();
	return valid && retirement.GetFrameResources().Prepare(2) && CheckPreferences();
}

bool NEMTests::TestFrameBufferingGPU(bool hardware) {

	ComPtr<ID3D12Debug> debug;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return false;
	debug->EnableDebugLayer();
	ComPtr<ID3D12Debug1> gpuDebug;
	if (SUCCEEDED(debug.As(&gpuDebug))) gpuDebug->SetEnableGPUBasedValidation(TRUE);
	ComPtr<IDXGIFactory6> factory;
	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<ID3D12Device> device;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
	if (hardware) {
		for (UINT index = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(index,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++index) {
			DXGI_ADAPTER_DESC1 description{};
			adapter->GetDesc1(&description);
			if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
				SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) break;
			adapter.Reset();
		}
	} else {
		if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) return false;
		D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
	}
	if (!device) return false;
	const uint32_t previousCount = Engine::GraphicsFrameState::GetActiveCount();
	const uint32_t previousIndex = Engine::GraphicsFrameState::GetCurrentIndex();
	Engine::GraphicsFrameState::SetActiveCount(2);
	bool valid = CheckRepeatedFrameChanges(device.Get());
	Engine::GraphicsFrameState::SetActiveCount(previousCount);
	Engine::GraphicsFrameState::SetCurrentIndex(previousIndex);
	ComPtr<ID3D12InfoQueue> info;
	if (FAILED(device.As(&info))) return false;
	for (UINT64 index = 0; index < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
		SIZE_T size = 0;
		info->GetMessage(index, nullptr, &size);
		std::vector<std::byte> storage(size);
		auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
		if (SUCCEEDED(info->GetMessage(index, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
			std::cerr << message->pDescription << '\n';
			valid = false;
		}
	}
	return valid;
}
