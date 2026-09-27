#include "GPUPipelineRetirementTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneGeometryUtility.h>
#include <Externals/DirectX12/d3dx12.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

bool NEMTests::CheckHiZSampleBounds(ID3D12Device* device, ID3D12CommandQueue* queue) {

	using namespace Engine;
	struct Input {
		float rectangle[4];
		uint32_t width;
		uint32_t height;
		uint32_t mipCount;
		uint32_t padding = 0;
		Matrix4x4 transform = Matrix4x4::Identity();
		Vector4 cone = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
		Vector4 cameraRadius = Vector4(0.0f, 0.0f, 10.0f, 1.0f);
	};
	static_assert(sizeof(Input) == 128);
	struct Output {
		std::array<uint32_t, 6> bounds;
		float scale;
		uint32_t coneVisible;
		uint32_t projectionCovered;
	};
	static_assert(sizeof(Output) == 36);
	// 元pixelで範囲を指定し、ShaderにはUVで渡す
	std::array<Input, 5> inputs{{
		{ { 1.8f, 1.8f, 4.2f, 4.2f }, 64, 64, 7 },
		{ { 3.9f, 1.8f, 4.05f, 4.2f }, 65, 64, 7 },
		{ { 62.9f, 1.8f, 64.9f, 4.2f }, 65, 63, 7 },
		{ { 0.0f, 0.0f, 1.0f, 1.0f }, 1, 1, 1 },
		{ { 0.0f, 0.0f, 40.0f, 40.0f }, 64, 64, 3 }
	}};
	// pixelMin.xy、pixelMax.xy、Mip、四隅で覆えるか
	const std::array<std::array<uint32_t, 6>, 5> expected{{
		{ 0, 0, 1, 1, 2, 1 }, { 0, 0, 1, 1, 2, 1 }, { 15, 0, 15, 1, 2, 1 },
		{ 0, 0, 0, 0, 0, 1 }, { 0, 0, 10, 10, 2, 0 }
	}};
	// せん断・反転・退化・回転後の球も変換先を包む
	inputs[1].transform.m[0][1] = 1.0f;
	inputs[2].transform.m[0][0] = -2.0f;
	inputs[2].transform.m[1][1] = 3.0f;
	inputs[3].transform.m[0][0] = inputs[3].transform.m[1][1] = inputs[3].transform.m[2][2] = 0.0f;
	inputs[4].transform.m[0][0] = inputs[4].transform.m[1][1] = 0.0f;
	inputs[4].transform.m[0][1] = 2.0f;
	inputs[4].transform.m[1][0] = -3.0f;
	inputs[1].cameraRadius.z = -10.0f;
	// 反転面の中心は背向きでも、球内には正面向きの点が残る
	inputs[2].cameraRadius = Vector4(1.0f, 0.0f, 0.1f, 0.5f);
	inputs[3].cameraRadius.z = -0.5f;
	inputs[4].cone.w = 0.5f;
	inputs[4].cameraRadius.z = -10.0f;
	const std::array<uint32_t, 5> coneVisible{ 1, 0, 1, 1, 0 };
	const size_t outputSize = sizeof(Output) * inputs.size();
	std::array<float, 5> scales{};
	for (size_t index = 0; index < inputs.size(); ++index) {
		const auto& transform = inputs[index].transform;
		scales[index] = RaytracingSceneGeometryUtility::GetMatrixMaxScale(transform);
		// 単位球上の方向を変換し、CPUの半径が不足しないことを確かめる
		for (int latitude = 0; latitude <= 16; ++latitude) {
			for (int longitude = 0; longitude < 32; ++longitude) {
				const float theta = latitude * 3.14159265f / 16.0f;
				const float phi = longitude * 3.14159265f / 16.0f;
				const Vector3 point(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
				if (Vector3::Transform(point, transform).Length() > scales[index] + 0.00001f) return false;
			}
		}
	}
	for (auto& input : inputs) {
		input.rectangle[0] /= input.width;
		input.rectangle[2] /= input.width;
		input.rectangle[1] /= input.height;
		input.rectangle[3] /= input.height;
	}
	TestDirectory directory("HiZBounds");
	const auto sourcePath = directory.GetPath() / "bounds.CS.hlsl";
	{
		// 製品と同じhelperを実GPUで実行する
		std::ofstream source(sourcePath, std::ios::binary);
		source << "#include \"" << Algorithm::PathToUTF8(
			RuntimePaths::GetEngineAssetsRoot() / "Shaders/Builtin/Common/CullingHelpers.hlsli") << "\"\n"
			"struct Input { float4 rectangle; uint2 size; uint mipCount; uint padding; row_major float4x4 transform;\n"
			" float4 cone; float4 cameraRadius; };\n"
			"StructuredBuffer<Input> gInput : register(t0);\n"
			"RWStructuredBuffer<uint> gOutput : register(u0);\n"
			"[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
			" Input value = gInput[id.x]; uint mip; uint2 lo, hi;\n"
			" bool covered = ResolveHiZSampleBounds(value.size, value.mipCount, value.rectangle.xy, value.rectangle.zw, mip, lo, hi);\n"
			" uint offset = id.x * 9; gOutput[offset] = lo.x; gOutput[offset + 1] = lo.y;\n"
			" gOutput[offset + 2] = hi.x; gOutput[offset + 3] = hi.y;\n"
			" gOutput[offset + 4] = mip; gOutput[offset + 5] = covered ? 1 : 0;\n"
			" gOutput[offset + 6] = asuint(GetMatrixMaxScale(value.transform));\n"
			" float3 camera = mul(float4(value.cameraRadius.xyz, 0), value.transform).xyz;\n"
			" gOutput[offset + 7] = IsTransformedNormalConeVisible(value.cone.xyz, value.cone.w,\n"
			" camera, value.cameraRadius.w, (float3x3)value.transform) ? 1 : 0;\n"
			// 画面端の球を包む立方体の投影範囲が欠けないか確認する
			" float4x4 view = (float4x4)1;\n"
			" float4x4 projection = (float4x4)0; projection[0][0] = 1; projection[1][1] = 1;\n"
			" projection[2][2] = 100.0 / 99.0; projection[2][3] = 1; projection[3][2] = -100.0 / 99.0;\n"
			" float3 center = float3((id.x < 3) ? 1.0 : -1.0, 2.0 + id.x, 10.0); center.x *= 5.0 + id.x;\n"
			" float2 uvMin, uvMax; float depth;\n"
			" bool projectionCovered = CalcSphereOcclusionProjection(projection, view, 1.0, float2(1,1),\n"
			" float2(64,64), float3(0,0,1), center, 1.0, uvMin, uvMax, depth);\n"
			" for (uint corner = 0; corner < 8; ++corner) {\n"
			" float3 pointValue = float3((corner & 1) != 0, (corner & 2) != 0, (corner & 4) != 0) * 2.0 - 1.0;\n"
			" float3 pointValueWorld = center + pointValue;\n"
			" float4 clip = mul(float4(pointValueWorld,1), projection);\n"
			" float2 uv = saturate(clip.xy / clip.w * float2(0.5,-0.5) + 0.5);\n"
			" projectionCovered = projectionCovered && all(uv >= uvMin - 0.00001) && all(uv <= uvMax + 0.00001); }\n"
			" gOutput[offset + 8] = projectionCovered ? 1 : 0; }\n";
		if (!source) return false;
	}
	DxShaderCompiler compiler;
	compiler.Init();
	const auto shader = compiler.CompileShader(sourcePath.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
	if (!shader.IsValid()) return false;
	CD3DX12_ROOT_PARAMETER parameters[2];
	parameters[0].InitAsShaderResourceView(0);
	parameters[1].InitAsUnorderedAccessView(0);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, parameters);
	ComPtr<ID3DBlob> signature, errors;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
		FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = root.Get();
	desc.CS = { shader.GetBytecodePointer(), shader.GetBytecodeSize() };
	if (FAILED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)))) return false;
	ComPtr<ID3D12Resource> input, output, readback;
	DxUtils::CreateUploadBufferResource(device, input, sizeof(inputs));
	DxUtils::CreateUavBufferResource(device, output, outputSize);
	DxUtils::CreateReadbackBufferResource(device, readback, outputSize);
	void* mapped = nullptr;
	const D3D12_RANGE empty{ 0, 0 };
	if (FAILED(input->Map(0, &empty, &mapped))) return false;
	std::memcpy(mapped, inputs.data(), sizeof(inputs));
	input->Unmap(0, nullptr);
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Fence> fence;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	const auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commands->ResourceBarrier(1, &toWrite);
	commands->SetComputeRootSignature(root.Get());
	commands->SetComputeRootShaderResourceView(0, input->GetGPUVirtualAddress());
	commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
	commands->Dispatch(static_cast<uint32_t>(inputs.size()), 1, 1);
	const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commands->ResourceBarrier(1, &toRead);
	commands->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, outputSize);
	if (FAILED(commands->Close())) return false;
	ID3D12CommandList* lists[]{ commands.Get() };
	queue->ExecuteCommandLists(1, lists);
	if (FAILED(queue->Signal(fence.Get(), 1))) return false;
	HANDLE completed = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!completed) return false;
	const HRESULT wait = fence->SetEventOnCompletion(1, completed);
	const DWORD result = SUCCEEDED(wait) ? WaitForSingleObject(completed, 30000) : WAIT_FAILED;
	CloseHandle(completed);
	if (result != WAIT_OBJECT_0) return false;
	const D3D12_RANGE range{ 0, outputSize };
	if (FAILED(readback->Map(0, &range, &mapped))) return false;
	std::array<Output, 5> results{};
	std::memcpy(results.data(), mapped, outputSize);
	readback->Unmap(0, &empty);
	bool valid = true;
	for (size_t index = 0; index < results.size(); ++index) {
		valid &= results[index].bounds == expected[index] && results[index].coneVisible == coneVisible[index] &&
			results[index].projectionCovered == 1 && std::isfinite(results[index].scale) &&
			std::abs(results[index].scale - scales[index]) <= 0.00001f;
	}
	if (!valid) std::cerr << "Culling bounds differ from the conservative CPU result\n";
	return valid;
}
