#include "GPUFilterTestFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

	// GPU操作の失敗を検証エラーにする
	void Require(HRESULT result) {

		if (FAILED(result)) throw std::runtime_error("Filter検証用GPU資源を作成できません");
	}

	// 画像形式に応じた一画素の成分数を返す
	uint32_t ComponentCount(DXGI_FORMAT format) {

		return format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 4u : format == DXGI_FORMAT_R32G32_FLOAT ? 2u : 1u;
	}

	struct TransferTexture {

		ComPtr<ID3D12Resource> resource;
		ComPtr<ID3D12Resource> transfer;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		uint64_t bytes = 0;
	};

	// 行間の配置も含めて転送先を確保する
	TransferTexture CreateTexture(ID3D12Device* device, uint32_t width, uint32_t height,
		DXGI_FORMAT format, bool output, bool structured = false) {

		TransferTexture texture;
		const auto flags = output ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
		const auto desc = structured ? CD3DX12_RESOURCE_DESC::Buffer(uint64_t(width) * height * ComponentCount(format) * 4u, flags) :
			CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0, flags);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		Require(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(&texture.resource)));
		if (structured) {
			texture.bytes = desc.Width;
			texture.footprint.Footprint.RowPitch = width * ComponentCount(format) * 4u;
		} else device->GetCopyableFootprints(&desc, 0, 1, 0, &texture.footprint, nullptr, nullptr, &texture.bytes);
		if (output) DxUtils::CreateReadbackBufferResource(device, texture.transfer, texture.bytes);
		else DxUtils::CreateUploadBufferResource(device, texture.transfer, texture.bytes);
		return texture;
	}

	// この検証で提出した処理だけを待つ
	void FinishCommands(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commands) {

		Require(commands->Close());
		ID3D12CommandList* lists[]{ commands };
		queue->ExecuteCommandLists(1, lists);
		ComPtr<ID3D12Fence> fence;
		Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		Require(queue->Signal(fence.Get(), 1));
		struct Event {
			HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			~Event() { if (value) CloseHandle(value); }
		} event;
		if (!event.value) throw std::runtime_error("Filter検証の待機先を作成できません");
		Require(fence->SetEventOnCompletion(1, event.value));
		if (WaitForSingleObject(event.value, 30000) != WAIT_OBJECT_0) throw std::runtime_error("Filter検証が完了しません");
	}
}

std::vector<NEMTests::FilterTestTexture> NEMTests::RunFilterShader(ID3D12Device* device, ID3D12CommandQueue* queue,
	const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const FilterTestTexture> inputs,
	std::span<const DXGI_FORMAT> outputFormats, std::span<const float> constants, double* gpuMs, const FilterTestLayout* layout) {

	Engine::DxShaderCompiler compiler;
	compiler.Init();
	const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", layout ? layout->entry : L"main", Engine::ShaderStage::CS);
	if (!shader.IsValid()) throw std::runtime_error("比較するFilterをコンパイルできません");
	CD3DX12_DESCRIPTOR_RANGE ranges[2];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 26, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);
	CD3DX12_ROOT_PARAMETER parameters[3];
	parameters[0].InitAsDescriptorTable(1, &ranges[0]);
	parameters[1].InitAsDescriptorTable(1, &ranges[1]);
	parameters[2].InitAsConstants(static_cast<UINT>(constants.size()), layout ? layout->constantRegister : 1);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(3, parameters);
	ComPtr<ID3DBlob> signature, errors;
	Require(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
	ComPtr<ID3D12RootSignature> root;
	Require(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)));
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = { shader.GetBytecodePointer(), shader.GetBytecodeSize() };
	ComPtr<ID3D12PipelineState> pipeline;
	Require(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
	Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands)));
	ComPtr<ID3D12DescriptorHeap> heap;
	const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 30,
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
	Require(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
	const auto stride = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
	std::array<TransferTexture, 26> sources;
	std::array<TransferTexture, 4> destinations;
	const D3D12_RANGE noRead{ 0, 0 };

	// 比較するShaderへ全く同じ画像を転送する
	for (size_t index = 0; index < 26; ++index) {
		const auto found = layout ? std::find(layout->inputSlots.begin(), layout->inputSlots.end(), uint32_t(index)) : std::span<const uint32_t>::iterator{};
		const size_t inputIndex = layout ? size_t(found - layout->inputSlots.begin()) : index;
		const bool structured = layout && std::find(layout->structuredSlots.begin(), layout->structuredSlots.end(), uint32_t(index)) != layout->structuredSlots.end();
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = inputIndex < inputs.size() ? inputs[inputIndex].format : DXGI_FORMAT_R32G32B32A32_FLOAT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		if (inputIndex < inputs.size()) {
			const auto& input = inputs[inputIndex];
			auto& target = sources[index];
			target = CreateTexture(device, input.width, input.height, input.format, false, structured);
			void* mapped = nullptr;
			Require(target.transfer->Map(0, &noRead, &mapped));
			const size_t rowSize = input.width * ComponentCount(input.format) * sizeof(float);
			for (uint32_t row = 0; row < input.height; ++row) {
				std::memcpy(static_cast<std::byte*>(mapped) + row * target.footprint.Footprint.RowPitch,
					reinterpret_cast<const std::byte*>(input.pixels.data()) + row * rowSize, rowSize);
			}
			target.transfer->Unmap(0, nullptr);
			const CD3DX12_TEXTURE_COPY_LOCATION destination(target.resource.Get(), 0), source(target.transfer.Get(), target.footprint);
			if (structured) commands->CopyBufferRegion(target.resource.Get(), 0, target.transfer.Get(), 0, target.bytes);
			else commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commands->ResourceBarrier(1, &barrier);
		}
		if (structured && inputIndex < inputs.size()) {
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srv.Buffer = {};
			srv.Buffer.NumElements = inputs[inputIndex].width * inputs[inputIndex].height;
			srv.Buffer.StructureByteStride = ComponentCount(inputs[inputIndex].format) * 4u;
		}
		device->CreateShaderResourceView(sources[index].resource.Get(), &srv,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(index), stride));
	}
	for (size_t index = 0; index < 4; ++index) {
		const bool structured = layout && layout->structuredOutput && index == 0;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = index < outputFormats.size() ? outputFormats[index] : DXGI_FORMAT_R32G32B32A32_FLOAT;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		if (index < outputFormats.size()) destinations[index] = CreateTexture(device, width, height, uav.Format, true, structured);
		if (structured && index < outputFormats.size()) {
			uav.Format = DXGI_FORMAT_UNKNOWN;
			uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uav.Buffer = {};
			uav.Buffer.NumElements = width * height;
			uav.Buffer.StructureByteStride = ComponentCount(outputFormats[index]) * 4u;
		}
		device->CreateUnorderedAccessView(destinations[index].resource.Get(), nullptr, &uav,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(26 + index), stride));
	}
	ID3D12DescriptorHeap* heaps[]{ heap.Get() };
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootSignature(root.Get());
	commands->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
	commands->SetComputeRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), 26, stride));
	commands->SetComputeRoot32BitConstants(2, static_cast<UINT>(constants.size()), constants.data(), 0);
	ComPtr<ID3D12QueryHeap> queries;
	ComPtr<ID3D12Resource> timestamps;
	if (gpuMs) {
		const D3D12_QUERY_HEAP_DESC queryDesc{ D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0 };
		Require(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries)));
		DxUtils::CreateReadbackBufferResource(device, timestamps, sizeof(uint64_t) * 2);
		commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
	}
	// 同じ入力で反復し提出や転送の時間を比較から外す
	const uint32_t repetitions = gpuMs ? 32u : 1u;
	for (uint32_t iteration = 0; iteration < repetitions; ++iteration) {
		commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
	}
	if (gpuMs) {
		commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
		commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timestamps.Get(), 0);
	}
	for (size_t index = 0; index < outputFormats.size(); ++index) {
		auto& output = destinations[index];
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands->ResourceBarrier(1, &barrier);
		const CD3DX12_TEXTURE_COPY_LOCATION destination(output.transfer.Get(), output.footprint), source(output.resource.Get(), 0);
		if (layout && layout->structuredOutput && index == 0) commands->CopyBufferRegion(output.transfer.Get(), 0, output.resource.Get(), 0, output.bytes);
		else commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	}
	FinishCommands(device, queue, commands.Get());
	if (gpuMs) {
		uint64_t frequency = 0;
		Require(queue->GetTimestampFrequency(&frequency));
		const D3D12_RANGE range{ 0, sizeof(uint64_t) * 2 };
		void* mapped = nullptr;
		Require(timestamps->Map(0, &range, &mapped));
		const auto* values = static_cast<const uint64_t*>(mapped);
		*gpuMs = double(values[1] - values[0]) * 1000.0 / double(frequency) / repetitions;
		timestamps->Unmap(0, &noRead);
	}

	// 行間の詰め物を除いて全成分を比較できるようにする
	std::vector<FilterTestTexture> result;
	for (size_t index = 0; index < outputFormats.size(); ++index) {
		auto& target = destinations[index];
		FilterTestTexture output{ width, height, outputFormats[index] };
		const size_t rowValues = width * ComponentCount(output.format);
		output.pixels.resize(rowValues * height);
		const D3D12_RANGE range{ 0, static_cast<SIZE_T>(target.bytes) };
		void* mapped = nullptr;
		Require(target.transfer->Map(0, &range, &mapped));
		for (uint32_t row = 0; row < height; ++row) {
			std::memcpy(output.pixels.data() + row * rowValues,
				static_cast<const std::byte*>(mapped) + row * target.footprint.Footprint.RowPitch, rowValues * sizeof(float));
		}
		target.transfer->Unmap(0, &noRead);
		result.push_back(std::move(output));
	}
	return result;
}

bool NEMTests::CheckFilterTransitionBatch(ID3D12Device* device, ID3D12CommandQueue* queue) {

	using namespace Engine;
	GraphicsResourceRetirement retirement;
	RTVDescriptor targets;
	SRVDescriptor shaders;
	targets.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE });
	shaders.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	targets.SetRetirementQueue(retirement);
	shaders.SetRetirementQueue(retirement);
	auto& profiler = FrameProfiler::GetInstance();
	profiler.BeginFrame(0.016f, 0.0f);
	{
		std::array<RenderTexture2D, 10> textures;
		std::array<RenderTexture2D*, 12> batch{};
		for (size_t index = 0; index < textures.size(); ++index) {
			RenderTextureCreateDesc desc;
			desc.width = desc.height = 4;
			desc.createUAV = true;
			textures[index].Create(device, &targets, &shaders, desc);
			batch[index] = &textures[index];
		}
		batch[10] = &textures[0];
		DxCommand commands;
		commands.Create(device);
		const auto read = static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		for (uint32_t iteration = 0; iteration < 2; ++iteration) {
			// 八枚を超える集合でも重複した遷移を発行しない
			RenderTexture2D::TransitionAll(commands, batch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			RenderTexture2D::TransitionAll(commands, batch, read);
			RenderTexture2D::TransitionAll(commands, batch, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			textures[0].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commands.TransitionBarriers(textures[0].GetResource(), read, read);
		}
		FinishCommands(device, queue, commands.GetCommandList());
		for (const auto& texture : textures) {
			if (texture.GetCurrentState() != read) return false;
		}
	}
	retirement.Seal(1);
	retirement.Collect(1);
	profiler.BeginFrame(0.016f, 0.016f);
	const auto& statistics = profiler.GetRenderingStatistics();
	return statistics.barrierCallCount == 8 && statistics.transitionBarrierCount == 40 &&
		statistics.uavBarrierCount == 0 && targets.GetUseDescriptorCount() == 0 && shaders.GetUseDescriptorCount() == 0;
}
