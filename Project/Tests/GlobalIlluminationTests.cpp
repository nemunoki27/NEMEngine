#include "GlobalIlluminationTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/GlobalIllumination/GlobalIlluminationSettings.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Rendering/Core/GraphicsFeatureSelection.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineBuilder.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>

// c++
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <fstream>
#include <iostream>

// DirectX
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

//============================================================================
//	GlobalIlluminationTests functions
//============================================================================

namespace {

	bool CheckGIProbeIntegration(ID3D12Device* device);
}

bool NEMTests::TestGlobalIllumination() {

	using namespace Engine;
	// 非対応GPUでも希望値は保持する
	GraphicsFeaturePreferences preferences;
	preferences.globalIllumination.enabled = true;
	GraphicsFeatureSupport support;
	if (GraphicsFeatureSelection::Resolve(support, preferences).useGlobalIllumination) return false;
	support.highestShaderModel = D3D_SHADER_MODEL_6_6;
	support.raytracingTier = D3D12_RAYTRACING_TIER_1_1;
	if (!GraphicsFeatureSelection::Resolve(support, preferences).useGlobalIllumination) return false;
	preferences.allowInlineRayTracing = false;
	preferences.allowDispatchRays = false;
	if (!GraphicsFeatureSelection::Resolve(support, preferences).useGlobalIllumination) return false;

	// 不正な値をGPUへ渡さない
	GlobalIlluminationSettings settings;
	settings.probeSpacing = std::numeric_limits<float>::quiet_NaN();
	settings.maxRayDistance = -1.0f;
	settings.updateBudgetMilliseconds = std::numeric_limits<float>::infinity();
	settings.quality = UINT32_MAX;
	settings = GlobalIlluminationStorage::Validate(settings);
	if (!(settings.probeSpacing > 0.0f && settings.maxRayDistance > 0.0f &&
		settings.updateBudgetMilliseconds > 0.0f && settings.quality <= 2u)) return false;

	// Cameraの使用切替を保存して復元する
	PerspectiveCameraComponent camera;
	camera.useGlobalIllumination = false;
	nlohmann::json cameraData;
	to_json(cameraData, camera);
	PerspectiveCameraComponent restored;
	from_json(cameraData, restored);
	if (restored.useGlobalIllumination) return false;
	cameraData.erase("useGlobalIllumination");
	PerspectiveCameraComponent previous;
	from_json(cameraData, previous);
	if (!previous.useGlobalIllumination) return false;

	// GPUに依存せず全GI入口のDXILを検証
	DxShaderCompiler compiler;
	compiler.Init();
	const auto compile = [&](const wchar_t* file, const wchar_t* profile, const wchar_t* entry, ShaderStage stage) {

		const auto shader = compiler.CompileShader((RuntimePaths::GetEngineAssetPath("Shaders") / file).wstring(), profile, entry, stage);
		if (!shader.IsValid()) return false;
		// Probe更新へ画面依存の反射入力を混ぜない
		return stage != ShaderStage::Lib || std::none_of(shader.reflection.resources.begin(), shader.reflection.resources.end(),
			[](const auto& resource) { return resource.name.starts_with("gSource") || resource.name.starts_with("gReflectionHit"); });
	};
	for (const auto* entry : { L"GIProbeRayGen", L"GIMiss", L"GIClosestHit", L"GIAnyHit" }) {

		if (!compile(L"Builtin/GlobalIllumination/giProbeTrace.RT.hlsl", L"lib_6_6", entry, ShaderStage::Lib)) return false;
	}
	if (!compile(L"Builtin/GlobalIllumination/giProbeBlend.CS.hlsl", L"cs_6_0", L"main", ShaderStage::CS) ||
		!compile(L"Builtin/Lighting/deferredLightingGI.PS.hlsl", L"ps_6_0", L"main", ShaderStage::PS) ||
		!compile(L"Builtin/Lighting/deferredLightingGI.PS.hlsl", L"ps_6_6", L"mainShadowed", ShaderStage::PS)) return false;
	// Graphの定数取得と頂点式を実際にコンパイル
	for (const auto target : { ShaderGraphTarget::Mesh, ShaderGraphTarget::Primitive3D }) {

		auto graph = CreateDefaultSurfaceShaderGraph("GITest", target);
		graph.vertexOutputNode = Engine::UUID::New();
		graph.nodes.push_back({ .id = graph.vertexOutputNode, .kind = ShaderGraphNodeKind::VertexOutput });
		const auto output = ShaderGraphCompiler::Compile(graph, "surface.hlsli");
		if (!output.Succeeded() || output.giMaterialHLSL.empty() || output.giVertexHLSL.empty()) return false;
		const auto root = RuntimePaths::GetLibraryPath("Tests/GlobalIllumination") / std::to_string(static_cast<int>(target));
		std::filesystem::create_directories(root);
		const auto write = [&](const char* name, const std::string& source) {

			std::ofstream stream(root / name, std::ios::binary | std::ios::trunc);
			stream << source;
			return stream.good();
		};
		if (!write("surface.hlsli", output.surfaceHLSL) || !write("material.RT.hlsl", output.giMaterialHLSL) ||
			!write("vertex.CS.hlsl", output.giVertexHLSL) ||
			!write("reflection.RT.hlsl", "#define NEM_REFLECTION_GI\n" + output.rayTracingHLSL)) return false;
		if (!compiler.CompileShader((root / "material.RT.hlsl").wstring(), L"lib_6_6", L"GIMaterial", ShaderStage::Lib).IsValid() ||
			!compiler.CompileShader((root / "vertex.CS.hlsl").wstring(), L"cs_6_6", L"main", ShaderStage::CS).IsValid() ||
			!compiler.CompileShader((root / "reflection.RT.hlsl").wstring(), L"lib_6_6", L"ReflectionClosestHit", ShaderStage::Lib).IsValid()) return false;
	}
	return true;
}

bool NEMTests::TestGlobalIlluminationHardware() {

	using namespace Engine;
	// 対応GPUでRootSignatureとState Objectを検証
	ComPtr<ID3D12Debug> debug;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return false;
	debug->EnableDebugLayer();
	ComPtr<IDXGIFactory4> factory;
	ComPtr<ID3D12Device8> device;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
	for (UINT index = 0; ; ++index) {

		ComPtr<IDXGIAdapter1> adapter;
		if (FAILED(factory->EnumAdapters1(index, &adapter))) break;
		ComPtr<ID3D12Device8> candidate;
		if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&candidate)))) continue;
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{ D3D_SHADER_MODEL_6_6 };
		if (FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support, sizeof(support))) ||
			support.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
			FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
			shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6) continue;
		device = std::move(candidate);
		break;
	}
	if (!device) {

		std::cerr << "GI対応GPUが見つかりません\n";
		return false;
	}
	DxShaderCompiler compiler;
	compiler.Init();
	PipelineVariantDesc variant;
	variant.kind = PipelineVariantKind::Raytracing;
	variant.pipelineType = PipelineType::Raytracing;
	variant.rayGenerationExports = { "GIProbeRayGen" };
	variant.missExports = { "GIMiss" };
	variant.hitGroups = { { .exportName = "GIHitGroup", .closestHitExport = "GIClosestHit", .anyHitExport = "GIAnyHit" } };
	variant.maxPayloadSizeInBytes = 48;
	variant.maxRecursionDepth = 2;
	ShaderAsset shader;
	for (const auto* entry : { "GIProbeRayGen", "GIMiss", "GIClosestHit", "GIAnyHit" }) {

		shader.stages.push_back({ .stage = ShaderStage::Lib,
			.file = "Builtin/GlobalIllumination/giProbeTrace.RT.hlsl", .entry = entry, .profile = "lib_6_6" });
	}
	// 複数GraphのCallableを同じPipelineへ接続
	for (const auto target : { ShaderGraphTarget::Mesh, ShaderGraphTarget::Primitive3D }) {

		const auto graph = CreateDefaultSurfaceShaderGraph("GIHardware", target);
		const auto output = ShaderGraphCompiler::Compile(graph, "surface.hlsli");
		if (!output.Succeeded()) return false;
		const auto root = RuntimePaths::GetLibraryPath("Tests/GlobalIllumination") / std::to_string(static_cast<int>(target));
		std::filesystem::create_directories(root);
		std::string source = output.giMaterialHLSL;
		const std::string entry = "GIMaterial" + std::to_string(static_cast<int>(target));
		source.replace(source.find("void GIMaterial(") + 5u, 10u, entry);
		std::ofstream(root / "surface.hlsli", std::ios::binary | std::ios::trunc) << output.surfaceHLSL;
		std::ofstream(root / "hardware.RT.hlsl", std::ios::binary | std::ios::trunc) << source;
		shader.stages.push_back({ .stage = ShaderStage::Lib, .file = (root / "hardware.RT.hlsl").string(),
			.entry = entry, .profile = "lib_6_6" });
		variant.callableExports.push_back(entry);
	}
	PipelineStaticSamplerOverrideSet samplers;
	samplers.fillMissingSamplers = true;
	GraphicsResourceRetirement retirement;
	auto pipeline = RaytracingPipelineBuilder::Create(device.Get(), &compiler, variant, shader, &samplers);
	if (!pipeline) return false;
	pipeline->SetRetirementQueue(retirement);
	const auto dispatch = pipeline->BuildDispatchDesc(128, 32);
	const bool valid = pipeline->GetStateObject() && dispatch.RayGenerationShaderRecord.SizeInBytes > 0 &&
		dispatch.CallableShaderTable.SizeInBytes >= 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	pipeline.reset();
	retirement.Seal(1);
	retirement.Collect(1);
	return valid && CheckGIProbeIntegration(device.Get());
}

namespace {

	bool CheckGIProbeIntegration(ID3D12Device* device) {

		using namespace Engine;
		// 一定放射輝度なら全方向の照度はπ倍になる
		DxShaderCompiler compiler;
		compiler.Init();
		const auto shader = compiler.CompileShader(RuntimePaths::GetEngineAssetPath(
			"Shaders/Builtin/GlobalIllumination/giProbeBlend.CS.hlsl").wstring(), L"cs_6_0", L"main", ShaderStage::CS);
		if (!shader.IsValid()) return false;
		CD3DX12_DESCRIPTOR_RANGE ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 6);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 6);
		CD3DX12_ROOT_PARAMETER parameters[3];
		parameters[0].InitAsConstants(28, 0, 6);
		parameters[1].InitAsDescriptorTable(1, &ranges[0]);
		parameters[2].InitAsDescriptorTable(1, &ranges[1]);
		const CD3DX12_ROOT_SIGNATURE_DESC description(3, parameters);
		ComPtr<ID3DBlob> signature, errors;
		ComPtr<ID3D12RootSignature> root;
		ComPtr<ID3D12PipelineState> pipeline;
		if (FAILED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
			FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = root.Get();
		pipelineDesc.CS = { shader.GetBytecodePointer(), shader.GetBytecodeSize() };
		if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)))) return false;

		// 1個だけ内部ProbeのRayを混ぜて分類も検証
		std::vector<std::array<float, 4>> zero(8 * 64);
		std::vector<std::array<float, 4>> rays(256 * 8, { 1.0f, 0.5f, 0.25f, 10.0f });
		std::fill(rays.begin() + 256, rays.begin() + 512, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, -0.25f });
		ComPtr<ID3D12CommandQueue> queue;
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commands;
		ComPtr<ID3D12Fence> fence;
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) ||
			FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		std::array<ComPtr<ID3D12Resource>, 2> inputs, uploads, readbacks;
		std::array<ComPtr<ID3D12Resource>, 4> outputs;
		for (uint32_t index = 0; index < inputs.size(); ++index) {

			const auto texture = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, index ? 256 : 8, index ? 8 : 64, 1, 1);
			if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&inputs[index])))) return false;
			DxUtils::CreateUploadBufferResource(device, uploads[index], GetRequiredIntermediateSize(inputs[index].Get(), 0, 1));
			const D3D12_SUBRESOURCE_DATA source{ index ? rays.data() : zero.data(), static_cast<LONG_PTR>(texture.Width * 16),
				static_cast<LONG_PTR>(texture.Width * texture.Height * 16) };
			if (!UpdateSubresources(commands.Get(), inputs[index].Get(), uploads[index].Get(), 0, 0, 1, &source)) return false;
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(inputs[index].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commands->ResourceBarrier(1, &barrier);
		}
		for (auto& output : outputs) {

			const auto texture = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 8, 64, 1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) return false;
		}
		ComPtr<ID3D12DescriptorHeap> descriptors;
		const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 9, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
		DxUtils::MakeDescriptorHeap(descriptors, device, heapDesc);
		const UINT increment = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(descriptors->GetCPUDescriptorHandleForHeapStart());
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		for (uint32_t index = 0; index < 5; ++index) {

			device->CreateShaderResourceView(inputs[index == 4 ? 1 : 0].Get(), &srv, handle);
			handle.Offset(1, increment);
		}
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = srv.Format;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		for (auto& output : outputs) {

			device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, handle);
			handle.Offset(1, increment);
		}
		std::array<uint32_t, 28> constants{};
		constants[3] = std::bit_cast<uint32_t>(2.0f);
		constants[12] = 2; constants[13] = 1; constants[15] = 8; constants[16] = 256;
		constants[18] = 1; constants[19] = 1; constants[20] = std::bit_cast<uint32_t>(80.0f);
		ID3D12DescriptorHeap* heaps[]{ descriptors.Get() };
		commands->SetDescriptorHeaps(1, heaps);
		commands->SetComputeRootSignature(root.Get());
		commands->SetComputeRoot32BitConstants(0, 28, constants.data(), 0);
		commands->SetComputeRootDescriptorTable(1, descriptors->GetGPUDescriptorHandleForHeapStart());
		commands->SetComputeRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptors->GetGPUDescriptorHandleForHeapStart(), 5, increment));
		commands->Dispatch(8, 1, 1);

		// 実際のShader出力を読戻して解析解と比較
		std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> layouts{};
		for (uint32_t index = 0; index < readbacks.size(); ++index) {

			ID3D12Resource* output = outputs[index == 0 ? 0 : 3].Get();
			const auto texture = output->GetDesc();
			UINT64 bytes = 0;
			device->GetCopyableFootprints(&texture, 0, 1, 0, &layouts[index], nullptr, nullptr, &bytes);
			DxUtils::CreateReadbackBufferResource(device, readbacks[index], bytes);
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
			commands->ResourceBarrier(1, &barrier);
			const CD3DX12_TEXTURE_COPY_LOCATION target(readbacks[index].Get(), layouts[index]), source(output, 0);
			commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
		}
		if (FAILED(commands->Close())) return false;
		ID3D12CommandList* lists[]{ commands.Get() };
		queue->ExecuteCommandLists(1, lists);
		if (FAILED(queue->Signal(fence.Get(), 1)) || !DxDREDDiagnostics::WaitForFence(device, fence.Get(), 1, nullptr, "GIProbeIntegrationTest")) return false;
		void* mapped = nullptr;
		if (FAILED(readbacks[0]->Map(0, nullptr, &mapped))) return false;
		bool valid = true;
		for (uint32_t bin = 0; bin < 64; ++bin) {

			const auto* value = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped) + bin * layouts[0].Footprint.RowPitch);
			for (uint32_t channel = 0; channel < 3; ++channel) {

				const float expected = 3.14159265359f * (1.0f / static_cast<float>(1u << channel));
				valid &= std::isfinite(value[channel]) && std::abs(value[channel] - expected) < expected * 0.02f;
				valid &= value[4 + channel] == 0.0f;
			}
		}
		readbacks[0]->Unmap(0, nullptr);
		if (FAILED(readbacks[1]->Map(0, nullptr, &mapped))) return false;
		const auto* offsets = static_cast<const float*>(mapped);
		valid &= offsets[3] == 1.0f && offsets[7] == 0.0f;
		readbacks[1]->Unmap(0, nullptr);
		if (!valid) return false;
		// 視線反転と八面体の継ぎ目を実際のGPUで検証
		const auto samplingPath = RuntimePaths::GetLibraryPath("Tests/GlobalIllumination/sampling.CS.hlsl");
		std::ofstream(samplingPath, std::ios::binary | std::ios::trunc) <<
			"#include \"" << RuntimePaths::GetEngineAssetPath("Shaders/Builtin/GlobalIllumination/giProbeSampling.hlsli").generic_string() << "\"\n"
			"RWTexture2D<float4> samples : register(u0, space6);\n"
			"[numthreads(4,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
			"float3 n=id.x<2?float3(0,1,0):normalize(float3(id.x==2?-0.00001:0.00001,0,-1));\n"
			"samples[uint2(id.x,0)]=SampleGlobalIllumination(float3(1,1,1),n,id.x==0?float3(1,0,0):float3(-1,0,0));}\n";
		const auto sampling = compiler.CompileShader(samplingPath.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
		if (!sampling.IsValid()) return false;
		pipelineDesc.CS = { sampling.GetBytecodePointer(), sampling.GetBytecodeSize() };
		ComPtr<ID3D12PipelineState> samplingPipeline;
		if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&samplingPipeline)))) return false;
		if (FAILED(allocator->Reset()) || FAILED(commands->Reset(allocator.Get(), samplingPipeline.Get()))) return false;
		for (uint32_t index = 0; index < 4; ++index) {

			const auto before = index == 0 || index == 3 ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(outputs[index].Get(), before, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commands->ResourceBarrier(1, &barrier);
			device->CreateShaderResourceView(outputs[index].Get(), &srv,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptors->GetCPUDescriptorHandleForHeapStart(), index, increment));
		}
		ComPtr<ID3D12Resource> samples;
		const auto sampleDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 1, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &sampleDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&samples)))) return false;
		device->CreateUnorderedAccessView(samples.Get(), nullptr, &uav,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptors->GetCPUDescriptorHandleForHeapStart(), 5, increment));
		// 旧実装でも視線による変化が現れる値を渡す
		constants[23] = std::bit_cast<uint32_t>(0.1f);
		commands->SetDescriptorHeaps(1, heaps);
		commands->SetComputeRootSignature(root.Get());
		commands->SetComputeRoot32BitConstants(0, 28, constants.data(), 0);
		commands->SetComputeRootDescriptorTable(1, descriptors->GetGPUDescriptorHandleForHeapStart());
		commands->SetComputeRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptors->GetGPUDescriptorHandleForHeapStart(), 5, increment));
		commands->Dispatch(1, 1, 1);
		UINT64 sampleBytes = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT sampleLayout{};
		device->GetCopyableFootprints(&sampleDesc, 0, 1, 0, &sampleLayout, nullptr, nullptr, &sampleBytes);
		ComPtr<ID3D12Resource> sampleReadback;
		DxUtils::CreateReadbackBufferResource(device, sampleReadback, sampleBytes);
		const auto sampleBarrier = CD3DX12_RESOURCE_BARRIER::Transition(samples.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands->ResourceBarrier(1, &sampleBarrier);
		const CD3DX12_TEXTURE_COPY_LOCATION sampleTarget(sampleReadback.Get(), sampleLayout), sampleSource(samples.Get(), 0);
		commands->CopyTextureRegion(&sampleTarget, 0, 0, 0, &sampleSource, nullptr);
		if (FAILED(commands->Close())) return false;
		queue->ExecuteCommandLists(1, lists);
		if (FAILED(queue->Signal(fence.Get(), 2)) || !DxDREDDiagnostics::WaitForFence(device, fence.Get(), 2, nullptr, "GIProbeSamplingTest")) return false;
		if (FAILED(sampleReadback->Map(0, nullptr, &mapped))) return false;
		const auto* values = static_cast<const float*>(mapped);
		valid = values[0] > 0.0f && values[3] > 0.0f;
		for (uint32_t channel = 0; channel < 4; ++channel) {
			valid &= std::isfinite(values[channel]) && std::abs(values[channel] - values[4 + channel]) < 1e-5f;
			valid &= std::isfinite(values[8 + channel]) && std::abs(values[8 + channel] - values[12 + channel]) < 0.001f;
		}
		sampleReadback->Unmap(0, nullptr);
		if (!valid) std::cerr << "GIの視線不変性または方向補間が成立しません\n";
		return valid;
	}
}
