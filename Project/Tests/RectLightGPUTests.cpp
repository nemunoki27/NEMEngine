#include "RectLightTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <iostream>
// directX
#include <DirectXTex.h>

namespace {

	using namespace Engine;
	using Results = std::array<std::array<float, 4>, 19>;

	// LUTと実際の面積積分を同じGPUで比較する
	class RectLightGPUFixture {
	public:

		bool Init();
		bool Run(Results& results);
		bool Benchmark();
	private:

		ComPtr<ID3D12Device> device_;
		ComPtr<ID3D12CommandQueue> queue_;
		ComPtr<ID3D12DescriptorHeap> descriptors_;
		ComPtr<ID3D12CommandAllocator> allocator_;
		ComPtr<ID3D12GraphicsCommandList> commands_;
		ComPtr<ID3D12Fence> fence_;
		ComPtr<ID3D12RootSignature> root_;
		ComPtr<ID3D12PipelineState> pipeline_;
		std::array<ComPtr<ID3D12PipelineState>, 2> benchmarkPipelines_;
		std::array<ComPtr<ID3D12Resource>, 2> textures_;
		std::array<ComPtr<ID3D12Resource>, 2> uploads_;
		uint64_t signal_ = 0;

		// 浮動小数点の参照画像を色変換せずGPUへ転送する
		bool UploadTable(size_t index, const char* file);
		// テストが発行したコマンドだけを完了まで待つ
		bool Submit();
	};

	bool RectLightGPUFixture::UploadTable(size_t index, const char* file) {

		DirectX::ScratchImage image;
		const auto path = RuntimePaths::GetEngineAssetPath(std::string("Textures/Lighting/LTC/") + file);
		if (FAILED(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image))) return false;
		const auto* pixels = image.GetImage(0, 0, 0);
		const auto description = CD3DX12_RESOURCE_DESC::Tex2D(image.GetMetadata().format, 64, 64, 1, 1);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textures_[index])))) return false;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
		UINT64 bytes = 0;
		device_->GetCopyableFootprints(&description, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
		DxUtils::CreateUploadBufferResource(device_.Get(), uploads_[index], bytes);
		void* mapped = nullptr;
		if (FAILED(uploads_[index]->Map(0, nullptr, &mapped))) return false;
		for (size_t row = 0; row < 64; ++row) {
			std::memcpy(static_cast<uint8_t*>(mapped) + layout.Offset + row * layout.Footprint.RowPitch,
				pixels->pixels + row * pixels->rowPitch, pixels->rowPitch);
		}
		uploads_[index]->Unmap(0, nullptr);
		const CD3DX12_TEXTURE_COPY_LOCATION source(uploads_[index].Get(), layout);
		const CD3DX12_TEXTURE_COPY_LOCATION destination(textures_[index].Get(), 0);
		commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(textures_[index].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		commands_->ResourceBarrier(1, &barrier);
		D3D12_SHADER_RESOURCE_VIEW_DESC view{};
		view.Format = description.Format;
		view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		view.Texture2D.MipLevels = 1;
		device_->CreateShaderResourceView(textures_[index].Get(), &view,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptors_->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(index),
				device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)));
		return true;
	}

	bool RectLightGPUFixture::Submit() {

		if (FAILED(commands_->Close())) return false;
		ID3D12CommandList* list = commands_.Get();
		queue_->ExecuteCommandLists(1, &list);
		if (FAILED(queue_->Signal(fence_.Get(), ++signal_))) return false;
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!event) return false;
		const bool armed = SUCCEEDED(fence_->SetEventOnCompletion(signal_, event));
		const bool completed = armed && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
		CloseHandle(event);
		return completed;
	}

	bool RectLightGPUFixture::Init() {

		// Editorと同じ高性能GPUで演算時間を比較する
		ComPtr<IDXGIFactory6> factory;
		ComPtr<IDXGIAdapter1> adapter;
		if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
			FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))) ||
			FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)))) return false;
		DXGI_ADAPTER_DESC1 adapterDescription{};
		adapter->GetDesc1(&adapterDescription);
		std::wcout << L"RectLight GPU: " << adapterDescription.Description << L"\n";
		const D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT};
		const D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
		if (FAILED(device_->CreateCommandQueue(&queue, IID_PPV_ARGS(&queue_))) ||
			FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors_))) ||
			FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))) ||
			FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
			FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;

		// 互換Descriptor Tableでも同じLTCテーブルを参照する
		CD3DX12_DESCRIPTOR_RANGE range;
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 126);
		CD3DX12_ROOT_PARAMETER parameters[3];
		parameters[0].InitAsConstants(8, 2);
		parameters[1].InitAsDescriptorTable(1, &range);
		parameters[2].InitAsUnorderedAccessView(0);
		const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		const CD3DX12_ROOT_SIGNATURE_DESC root(3, parameters, 1, &sampler);
		ComPtr<ID3DBlob> bytes, errors;
		if (FAILED(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors)) ||
			FAILED(device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(), IID_PPV_ARGS(&root_)))) return false;
		DxShaderCompiler compiler;
		compiler.Init();
		const auto path = RuntimePaths::GetEngineProjectRoot() / "Tests/Shaders/rectLightValidation.CS.hlsl";
		const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
		if (!shader.IsValid()) return false;
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
		pipeline.pRootSignature = root_.Get();
		pipeline.CS = {shader.GetBytecodePointer(), shader.GetBytecodeSize()};
		if (FAILED(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pipeline_))) ||
			!UploadTable(0, "ltcMatrix.dds") || !UploadTable(1, "ltcAmplitude.dds")) return false;
		const std::array entries{L"BenchmarkFourPoints", L"BenchmarkLTC"};
		for (size_t index = 0; index < entries.size(); ++index) {

			const auto compiled = compiler.CompileShader(path.wstring(), L"cs_6_0", entries[index], ShaderStage::CS);
			if (!compiled.IsValid()) return false;
			pipeline.CS = {compiled.GetBytecodePointer(), compiled.GetBytecodeSize()};
			if (FAILED(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&benchmarkPipelines_[index])))) return false;
		}
		return Submit();
	}

	bool RectLightGPUFixture::Run(Results& results) {

		if (FAILED(allocator_->Reset()) || FAILED(commands_->Reset(allocator_.Get(), pipeline_.Get()))) return false;
		ComPtr<ID3D12Resource> output, readback;
		const auto description = CD3DX12_RESOURCE_DESC::Buffer(sizeof(results), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) return false;
		DxUtils::CreateReadbackBufferResource(device_.Get(), readback, sizeof(results));
		ID3D12DescriptorHeap* descriptorHeap = descriptors_.Get();
		commands_->SetDescriptorHeaps(1, &descriptorHeap);
		commands_->SetComputeRootSignature(root_.Get());
		const std::array<uint32_t, 8> counts{0, 0, 0, 1, 1, 0, 1, 0};
		commands_->SetComputeRoot32BitConstants(0, 8, counts.data(), 0);
		commands_->SetComputeRootDescriptorTable(1, descriptors_->GetGPUDescriptorHandleForHeapStart());
		commands_->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
		commands_->Dispatch(1, 1, 1);
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands_->ResourceBarrier(1, &barrier);
		commands_->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, sizeof(results));
		if (!Submit()) return false;
		void* mapped = nullptr;
		const D3D12_RANGE range{0, sizeof(results)};
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		std::memcpy(results.data(), mapped, sizeof(results));
		const D3D12_RANGE empty{0, 0};
		readback->Unmap(0, &empty);
		return true;
	}
	bool RectLightGPUFixture::Benchmark() {

		if (FAILED(allocator_->Reset()) || FAILED(commands_->Reset(allocator_.Get(), nullptr))) return false;
		ComPtr<ID3D12QueryHeap> queries;
		const D3D12_QUERY_HEAP_DESC queryDescription{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4};
		if (FAILED(device_->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&queries)))) return false;
		ComPtr<ID3D12Resource> output, readback;
		const auto description = CD3DX12_RESOURCE_DESC::Buffer(16384u * 16u, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) return false;
		DxUtils::CreateReadbackBufferResource(device_.Get(), readback, sizeof(uint64_t) * 4);
		ID3D12DescriptorHeap* descriptorHeap = descriptors_.Get();
		commands_->SetDescriptorHeaps(1, &descriptorHeap);
		commands_->SetComputeRootSignature(root_.Get());
		const std::array<uint32_t, 8> counts{0, 0, 0, 1, 1, 0, 1, 0};
		commands_->SetComputeRoot32BitConstants(0, 8, counts.data(), 0);
		commands_->SetComputeRootDescriptorTable(1, descriptors_->GetGPUDescriptorHandleForHeapStart());
		commands_->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
		const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(output.Get());
		for (uint32_t mode = 0; mode < 2; ++mode) {

			commands_->SetPipelineState(benchmarkPipelines_[mode].Get());
			commands_->Dispatch(256, 1, 1);
			commands_->ResourceBarrier(1, &barrier);
			commands_->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, mode * 2);
			// 同じ出力先への書込をDispatchごとに完了させる
			for (uint32_t repeat = 0; repeat < 32; ++repeat) {
				commands_->Dispatch(256, 1, 1);
				commands_->ResourceBarrier(1, &barrier);
			}
			commands_->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, mode * 2 + 1);
		}
		commands_->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, readback.Get(), 0);
		if (!Submit()) return false;
		uint64_t frequency = 0;
		if (FAILED(queue_->GetTimestampFrequency(&frequency)) || frequency == 0) return false;
		void* mapped = nullptr;
		const D3D12_RANGE range{0, sizeof(uint64_t) * 4};
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		std::array<uint64_t, 4> ticks{};
		std::memcpy(ticks.data(), mapped, sizeof(ticks));
		const D3D12_RANGE empty{0, 0};
		readback->Unmap(0, &empty);
		std::cout << "RectLight GPU math, 16384 pixels, Debug Shader: four points="
			<< double(ticks[1] - ticks[0]) * 1000.0 / frequency / 32.0 << "ms LTC="
			<< double(ticks[3] - ticks[2]) * 1000.0 / frequency / 32.0 << "ms\n";
		return true;
	}
}

bool NEMTests::TestRectLightGPU() {

	RectLightGPUFixture fixture;
	Results first{}, second{};
	if (!fixture.Init() || !fixture.Run(first) || !fixture.Run(second)) return false;
	for (size_t index = 0; index < first.size(); ++index) {

		const auto& value = first[index];
		std::cout << "RectLight[" << index << "] diffuse=" << value[0] << " reference=" << value[1]
			<< " specular=" << value[2] << " reference=" << value[3] << '\n';
		for (float component : value) if (!std::isfinite(component) || component < -1e-5f) return false;
		// 拡散は厳密積分、鏡面はLTCと平均Fresnelの近似差を比較する
		if (std::abs(value[0] - value[1]) > std::max(0.0001f, value[1] * 0.02f) ||
			std::abs(value[2] - value[3]) > std::max({0.001f, value[3] * 0.25f, (value[1] + value[3]) * 0.02f}) || value != second[index]) return false;
	}
	// 遠距離の総出力が面積によらず逆二乗で届くことを確認する
	const float farDiffuse = 100.0f * 0.96f * 0.9801f / (3.14159265f * 3.14159265f * 10000.0f);
	if (std::abs(first[3][0] - farDiffuse) > farDiffuse * 0.005f) return false;
	// 距離と裏面の条件も実際のGPU出力で確認する
	if (first[0][0] <= first[1][0] || first[1][0] <= first[3][0] || first[11][0] <= first[0][0]) return false;
	for (size_t index : {5u, 6u, 7u, 8u, 9u, 10u, 14u}) {
		if (first[index][0] != 0.0f || first[index][2] != 0.0f) return false;
	}
	return fixture.Benchmark();
}
