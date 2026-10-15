#include "BakedLightingTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingRuntime.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingInput.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingStorage.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <chrono>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

bool NEMTests::TestBakedLightingRuntimeGPU(Engine::GraphicsCore& graphics, Engine::AssetDatabase& database,
	const Engine::BakedLightingData& source, const std::filesystem::path& directory) {

	using namespace Engine;
	ECSWorld world;
	const Entity receiver = world.CreateEntity();
	world.AddComponent<SceneObjectComponent>(receiver);
	world.AddComponent<TransformComponent>(receiver);
	world.AddComponent<MeshRendererComponent>(receiver);
	SceneInstance scene;
	scene.instanceID = Engine::UUID::New();
	scene.sceneAsset = AssetID{0x42414b4554455354ull, 2};
	auto& object = world.GetComponent<SceneObjectComponent>(receiver);
	object.localFileID = source.receivers.front().localFileID;
	object.sceneInstanceID = scene.instanceID;
	object.sourceAsset = scene.sceneAsset;
	world.GetComponent<MeshRendererComponent>(receiver).contributeBakedLighting = true;
	BakedLightingData data = source;
	data.sceneAsset = scene.sceneAsset;
	data.receivers.front().settings = BakedLightingInputBuilder::ReceiverSettings(world, receiver);
	std::string diagnostic;
	scene.header.bakedLighting = BakedLightingStorage::Save(database,
		RuntimePaths::ToAssetPath(directory / "runtime.bakedlighting.json"), data, diagnostic);
	scene.header.bakedLightingEnabled = true;
	if (!scene.header.bakedLighting) { std::cerr << diagnostic << '\n'; return false; }
	const auto shaderPath = directory / "sample.CS.hlsl";
	std::ofstream shader(shaderPath);
	shader << "#include \"" << (RuntimePaths::GetEngineAssetsRoot() /
		"Shaders/Builtin/Lighting/Baked/bakedLightingRuntime.hlsli").generic_string() << "\"\n"
		"RWStructuredBuffer<float4> gOutput : register(u0);\n"
		"cbuffer SampleConstants : register(b0) { float3 position; uint entity; float3 normal; float roughness; };\n"
		"[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
		"float3 color; float coverage; bool valid = SampleBakedIndirect(position,normal,64u,entity,color);\n"
		"gOutput[0]=float4(color,valid?1.0f:0.0f);\n"
		"valid=SampleBakedProbes(position+normal*0.2f,normal,color); gOutput[1]=float4(color,valid?1.0f:0.0f);\n"
		"valid=SampleBakedReflection(position,normal,roughness,color,coverage); gOutput[2]=float4(color,valid?coverage:0.0f); }\n";
	shader.close();
	auto& platform = graphics.GetDXObject();
	ComputePipelineDesc description;
	description.compute.file = shaderPath.generic_string();
	description.compute.entry = "main";
	description.compute.profile = "cs_6_0";
	auto pipeline = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), description);
	if (!pipeline) return false;
	struct SampleConstants {
		Vector3 position{0.0f, 0.0f, 0.0f};
		uint32_t entity = 0;
		Vector3 normal{0.0f, 1.0f, 0.0f};
		float roughness = 0.5f;
	};
	ViewConstantBuffer<SampleConstants> sampleConstants;
	sampleConstants.Init(platform.GetResourceRetirement(), platform.GetDevice());
	StructuredRWBuffer<Vector4> output;
	output.Init(platform.GetDevice(), &graphics.GetSRVDescriptor());
	output.EnsureCapacity(3);
	ComPtr<ID3D12Resource> readback;
	DxUtils::CreateReadbackBufferResource(platform.GetDevice(), readback, 3 * sizeof(Vector4));
	BakedLightingRuntime runtime;
	SceneExecutionContext context;
	context.world = &world;
	context.sceneInstance = &scene;
	context.assetDatabase = &database;
	const auto dispatch = [&]() {
		graphics.TickFrameServices();
		graphics.BeginRenderFrame();
		runtime.Update(graphics, context);
		const SampleConstants constants{Vector3{}, receiver.index + 1u};
		sampleConstants.Upload(constants);
		auto* dxCommand = platform.GetDxCommand();
		auto* command = dxCommand->GetCommandList();
		dxCommand->SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
		output.Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		command->SetComputeRootSignature(pipeline->GetRootSignature());
		command->SetPipelineState(pipeline->GetComputePipeline());
		std::vector<ComputeBindItem> bindings;
		for (const auto& entry : context.bufferRegistry.GetEntries()) bindings.push_back({entry.alias,
			entry.alias == "BakedLightingConstants" ? ComputeBindValueType::CBV : ComputeBindValueType::SRV,
			entry.gpuAddress, entry.srvGPUHandle});
		bindings.push_back({"SampleConstants", ComputeBindValueType::CBV, sampleConstants.GetGPUAddress()});
		bindings.push_back({"gOutput", ComputeBindValueType::UAV, output.GetGPUAddress(), output.GetUAVGPUHandle()});
		ComputeRootBinder{*pipeline}.Bind(command, bindings);
		command->Dispatch(1, 1, 1);
		output.Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		command->CopyBufferRegion(readback.Get(), 0, output.GetResource(), 0, 3 * sizeof(Vector4));
		graphics.SubmitRenderFrame();
		graphics.EndRenderFrame();
		// テスト結果の読み戻しだけGPU完了を待つ
		platform.WaitForGPU();
		std::array<Vector4, 3> values{};
		void* mapped = nullptr;
		const D3D12_RANGE range{0, sizeof(values)};
		if (FAILED(readback->Map(0, &range, &mapped))) return values;
		std::memcpy(values.data(), mapped, sizeof(values));
		const D3D12_RANGE empty{0, 0};
		readback->Unmap(0, &empty);
		return values;
	};
	std::array<Vector4, 3> values{};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	do {
		values = dispatch();
		if (values[0].w > 0.0f) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (std::chrono::steady_clock::now() < deadline);
	std::cout << "Runtime lightmap=" << values[0].x << ", probes=" << values[1].x << ", capture=" << values[2].x << '\n';
	if (values[0].x <= 0.0f || values[0].x <= values[0].y * 4.0f || values[1].w <= 0.0f || values[2].w <= 0.0f) return false;
	scene.header.bakedLightingEnabled = false;
	values = dispatch();
	if (values[0].w != 0.0f || values[1].w != 0.0f || values[2].w != 0.0f) return false;
	scene.header.bakedLightingEnabled = true;
	world.GetComponent<MeshRendererComponent>(receiver).visible = false;
	world.MarkComponentModified<MeshRendererComponent>(receiver);
	values = dispatch();
	return values[0].w == 0.0f && values[1].w == 0.0f && values[2].w == 0.0f;
}
