#include "BloomTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Bloom/BloomSettings.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

	using namespace Engine;
	using Pixel = std::array<float, 4>;

	// GPU検証の間だけ画像と現在の状態を所有する
	struct TestTexture {

		ComPtr<ID3D12Resource> resource;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		uint32_t width = 0, height = 0;
	};

	// 本番と同じShaderと半精度の中間画像を使う
	class BloomGPUFixture {
	public:

		bool Init();
		bool Check(uint32_t width, uint32_t height, uint32_t mode);
	private:

		ComPtr<ID3D12Device> device_;
		ComPtr<ID3D12CommandQueue> queue_;
		ComPtr<ID3D12RootSignature> root_;
		std::array<ComPtr<ID3D12PipelineState>, 4> pipelines_;
		ComPtr<ID3D12DescriptorHeap> descriptors_;
		ComPtr<ID3D12CommandAllocator> allocator_;
		ComPtr<ID3D12GraphicsCommandList> commands_;
		ComPtr<ID3D12Fence> fence_;
		uint32_t increment_ = 0, nextDescriptor_ = 0;
		uint64_t signal_ = 0;
		BloomSettings settings_;

		// 書込先の画像を作る
		bool CreateTexture(TestTexture& texture, uint32_t width, uint32_t height, bool half);
		// コマンド列の現在の状態から遷移する
		void Transition(TestTexture& texture, D3D12_RESOURCE_STATES state);
		// 各Dispatchに独立したDescriptorを割り当てる
		void Dispatch(uint32_t stage, TestTexture& source, TestTexture& extra, TestTexture& output);
	};

	bool BloomGPUFixture::Init() {

		if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)))) return false;
		const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT};
		if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)))) return false;
		CD3DX12_DESCRIPTOR_RANGE ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		CD3DX12_ROOT_PARAMETER parameters[4];
		parameters[0].InitAsConstants(4, 0);
		parameters[1].InitAsConstants(8, 1);
		parameters[2].InitAsDescriptorTable(1, &ranges[0]);
		parameters[3].InitAsDescriptorTable(1, &ranges[1]);
		const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		const CD3DX12_ROOT_SIGNATURE_DESC desc(4, parameters, 1, &sampler);
		ComPtr<ID3DBlob> bytes, errors;
		if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors)) ||
			FAILED(device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(), IID_PPV_ARGS(&root_)))) return false;
		DxShaderCompiler compiler;
		compiler.Init();
		const std::array names{"Prefilter", "Downsample", "Upsample", "Composite"};
		for (size_t i = 0; i < names.size(); ++i) {
			const auto path = RuntimePaths::GetEngineAssetPath("Shaders/Builtin/PostProcess/Bloom/bloom" + std::string(names[i]) + ".CS.hlsl");
			const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
			if (!shader.IsValid()) return false;
			D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
			pipeline.pRootSignature = root_.Get();
			pipeline.CS = {shader.GetBytecodePointer(), shader.GetBytecodeSize()};
			if (FAILED(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pipelines_[i])))) return false;
		}
		const D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
		if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors_))) ||
			FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))) ||
			FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
			FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
		increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return SUCCEEDED(commands_->Close());
	}

	bool BloomGPUFixture::CreateTexture(TestTexture& texture, uint32_t width, uint32_t height, bool half) {

		texture.width = width;
		texture.height = height;
		const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(half ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT,
			width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, texture.state, nullptr, IID_PPV_ARGS(&texture.resource)));
	}

	void BloomGPUFixture::Transition(TestTexture& texture, D3D12_RESOURCE_STATES state) {

		if (texture.state == state) return;
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, state);
		commands_->ResourceBarrier(1, &barrier);
		texture.state = state;
	}

	void BloomGPUFixture::Dispatch(uint32_t stage, TestTexture& source, TestTexture& extra, TestTexture& output) {

		Transition(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Transition(extra, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(descriptors_->GetCPUDescriptorHandleForHeapStart(), nextDescriptor_, increment_);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		for (auto* texture : {&source, &extra}) {
			srv.Format = texture->resource->GetDesc().Format;
			device_->CreateShaderResourceView(texture->resource.Get(), &srv, cpu);
			cpu.Offset(1, increment_);
		}
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav.Format = output.resource->GetDesc().Format;
		device_->CreateUnorderedAccessView(output.resource.Get(), nullptr, &uav, cpu);
		commands_->SetPipelineState(pipelines_[stage].Get());
		const std::array<float, 4> frame{float(output.width), float(output.height), 1.0f / output.width, 1.0f / output.height};
		const std::array<float, 8> values{settings_.threshold, settings_.softKnee, settings_.intensity, settings_.scatter,
			settings_.tint.r, settings_.tint.g, settings_.tint.b, 1.0f};
		commands_->SetComputeRoot32BitConstants(0, 4, frame.data(), 0);
		commands_->SetComputeRoot32BitConstants(1, 8, values.data(), 0);
		commands_->SetComputeRootDescriptorTable(2,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptors_->GetGPUDescriptorHandleForHeapStart(), nextDescriptor_, increment_));
		commands_->SetComputeRootDescriptorTable(3,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptors_->GetGPUDescriptorHandleForHeapStart(), nextDescriptor_ + 2, increment_));
		nextDescriptor_ += 3;
		commands_->Dispatch((output.width + 7) / 8, (output.height + 7) / 8, 1);
	}

	bool BloomGPUFixture::Check(uint32_t width, uint32_t height, uint32_t mode) {

		settings_ = BloomSettings{};
		if (mode == 3) settings_.intensity = 0.0f;
		if (mode == 4) settings_.scatter = 0.0f;
		TestTexture source, output;
		if (!CreateTexture(source, width, height, false) || !CreateTexture(output, width, height, false)) return false;
		const auto sizes = BuildBloomLevelSizes(width, height);
		std::vector<TestTexture> down(sizes.size()), up(sizes.size());
		for (size_t i = 0; i < sizes.size(); ++i) {
			if (!CreateTexture(down[i], sizes[i].width, sizes[i].height, true) ||
				!CreateTexture(up[i], sizes[i].width, sizes[i].height, true)) return false;
		}
		// 一様な面と小さい高輝度部分をそれぞれ確認する
		std::vector<Pixel> pixels(width * height, Pixel{mode == 1 ? 4.0f : 0.2f, mode == 1 ? 4.0f : 0.2f, mode == 1 ? 4.0f : 0.2f, 0.37f});
		if (mode >= 2) {
			for (uint32_t y = height / 2 - (height > 4 ? 2 : 0); y <= height / 2 + (height > 4 ? 1 : 0); ++y) {
				for (uint32_t x = mode == 4 ? 0 : width / 2 - (width > 4 ? 2 : 0);
					x <= (mode == 4 ? 3 : width / 2 + (width > 4 ? 1 : 0)); ++x) pixels[y * width + x] = {40, 20, 10, 0.37f};
			}
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
		UINT64 bytes = 0;
		const auto resourceDesc = source.resource->GetDesc();
		device_->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
		ComPtr<ID3D12Resource> upload, readback;
		DxUtils::CreateUploadBufferResource(device_.Get(), upload, bytes);
		DxUtils::CreateReadbackBufferResource(device_.Get(), readback, bytes);
		void* mapped = nullptr;
		const D3D12_RANGE empty{0, 0};
		if (FAILED(upload->Map(0, &empty, &mapped))) return false;
		for (uint32_t y = 0; y < height; ++y) std::memcpy(static_cast<std::byte*>(mapped) + y * layout.Footprint.RowPitch,
			pixels.data() + y * width, width * sizeof(Pixel));
		upload->Unmap(0, nullptr);
		if (FAILED(allocator_->Reset()) || FAILED(commands_->Reset(allocator_.Get(), nullptr))) return false;
		nextDescriptor_ = 0;
		ID3D12DescriptorHeap* heaps[]{descriptors_.Get()};
		commands_->SetDescriptorHeaps(1, heaps);
		commands_->SetComputeRootSignature(root_.Get());
		Transition(source, D3D12_RESOURCE_STATE_COPY_DEST);
		const CD3DX12_TEXTURE_COPY_LOCATION uploadSource(upload.Get(), layout), uploadDest(source.resource.Get(), 0);
		commands_->CopyTextureRegion(&uploadDest, 0, 0, 0, &uploadSource, nullptr);
		for (size_t i = 0; i < sizes.size(); ++i) Dispatch(i == 0 ? 0 : 1, i == 0 ? source : down[i - 1], source, down[i]);
		TestTexture* blurred = &down.back();
		for (size_t i = sizes.size() - 1; 0 < i; --i) {
			Dispatch(2, down[i - 1], *blurred, up[i - 1]);
			blurred = &up[i - 1];
		}
		Dispatch(3, source, *blurred, output);
		Transition(output, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const CD3DX12_TEXTURE_COPY_LOCATION resultSource(output.resource.Get(), 0), resultDest(readback.Get(), layout);
		commands_->CopyTextureRegion(&resultDest, 0, 0, 0, &resultSource, nullptr);
		if (FAILED(commands_->Close())) return false;
		ID3D12CommandList* lists[]{commands_.Get()};
		queue_->ExecuteCommandLists(1, lists);
		if (FAILED(queue_->Signal(fence_.Get(), ++signal_)) ||
			!DxDREDDiagnostics::WaitForFence(device_.Get(), fence_.Get(), signal_, nullptr, "BloomTest")) return false;
		const D3D12_RANGE range{0, static_cast<size_t>(bytes)};
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		bool valid = true;
		std::vector<Pixel> result(width * height);
		for (uint32_t y = 0; y < height; ++y) std::memcpy(result.data() + y * width,
			static_cast<const std::byte*>(mapped) + y * layout.Footprint.RowPitch, width * sizeof(Pixel));
		readback->Unmap(0, &empty);
		for (size_t i = 0; i < result.size(); ++i) {
			for (float value : result[i]) valid &= std::isfinite(value);
			valid &= std::abs(result[i][3] - 0.37f) < 0.0001f;
			if (mode == 0 || mode == 3) for (size_t c = 0; c < 3; ++c) valid &= std::abs(result[i][c] - pixels[i][c]) < 0.0001f;
			if (mode == 1) for (size_t c = 0; c < 3; ++c) valid &= std::abs(result[i][c] - 5.8f) < 0.02f;
		}
		if (mode == 2 && width > 32 && height > 32) {
			const Pixel nearby = result[(height / 2) * width + width / 2 + 12];
			valid &= nearby[0] > 0.201f && nearby[0] > nearby[1] && nearby[1] > nearby[2];
		}
		// 左端の光が反対側へ回り込まないことを確認する
		if (mode == 4) {
			valid &= result[(height / 2) * width + 5][0] > 0.201f;
			valid &= std::abs(result[(height / 2) * width + width - 2][0] - 0.2f) < 0.0001f;
		}
		if (!valid) std::cerr << "Bloom output mismatch mode=" << mode << " size=" << width << "x" << height << "\n";
		return valid;
	}
}

bool NEMTests::TestBloomGPU() {

	BloomGPUFixture fixture;
	if (!fixture.Init()) return false;
	for (uint32_t mode = 0; mode < 4; ++mode) {
		if (!fixture.Check(129, 65, mode) || !fixture.Check(65, 129, mode) || !fixture.Check(1, 1, mode)) return false;
	}
	return fixture.Check(257, 129, 4);
}
