#include "ParticleGPUSourceSpawnTests.h"
#include "ParticleGPUFixture.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Sources/ParticleGPUSourceResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>

using namespace Engine;
using namespace NEMTests;

namespace {

	struct SourceFixture {

		ParticleGPUFixture& fixture;
		StructuredInstanceBuffer<MeshVertex> vertices, previous;
		StructuredInstanceBuffer<uint32_t> indices, subMeshIDs;
		StructuredInstanceBuffer<ParticleSourceSubMeshGPU> subMeshes;
		StructuredInstanceBuffer<float> distribution;
		StructuredInstanceBuffer<Vector4> colors;
		StructuredInstanceBuffer<ParticleGPUPhase> phases;
		StructuredInstanceBuffer<ParticleGPUInstruction> instructions;
		StructuredInstanceBuffer<ParticleParentGPU> parents;
		StructuredRWBuffer<Vector4> states;
		StructuredRWBuffer<ParticleAttributesGPU> particles;
		RTVDescriptor renderTargets;
		RenderTexture2D image;
		std::vector<MeshVertex> vertexData;
		std::vector<uint32_t> indexData{0, 1, 2, 3, 4, 5};
		std::vector<Vector4> colorData{6, Vector4(0.25f, 0.5f, 0.75f, 1)};
		ParticleSourceSubMeshGPU subMesh;
		ParticleGPUSourceConstants constants;
		uint32_t count = 8192;

		// Shaderが参照する空領域も本番のDescriptorで用意する
		explicit SourceFixture(ParticleGPUFixture& target) : fixture(target) {

			auto* device = fixture.device.Get();
			auto* descriptors = &fixture.descriptors;
			vertices.Init(device, descriptors); previous.Init(device, descriptors);
			indices.Init(device, descriptors); subMeshIDs.Init(device, descriptors);
			subMeshes.Init(device, descriptors); distribution.Init(device, descriptors);
			colors.Init(device, descriptors); phases.Init(device, descriptors);
			instructions.Init(device, descriptors); parents.Init(device, descriptors);
			states.Init(device, descriptors); particles.Init(device, descriptors);
			states.EnsureCapacity(count * 2); particles.EnsureCapacity(count);
			renderTargets.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
			renderTargets.SetRetirementQueue(fixture.retirement);
			image.Create(device, &renderTargets, descriptors, {.width = 2, .height = 2,
				.format = DXGI_FORMAT_R32G32B32A32_FLOAT, .clearColor = Color4(0.25f, 0.5f, 0.75f, 1)});
			image.Transition(fixture.commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
			const float color[]{0.25f, 0.5f, 0.75f, 1};
			fixture.commands.GetCommandList()->ClearRenderTargetView(image.GetRenderTarget().rtvHandle, color, 0, nullptr);
			image.Transition(fixture.commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			const Vector3 positions[]{Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3(0, 2, 0),
				Vector3(0, 0, 1), Vector3(2, 0, 1), Vector3(0, 1, 1)};
			for (const Vector3 position : positions) {
				auto& vertex = vertexData.emplace_back();
				vertex.position = Vector4(position.x, position.y, position.z, 1);
				vertex.normal = Vector3(0, 1, 0);
			}
			constants.kind = 3;
			constants.candidateCount = 2;
			constants.vertexCount = 6;
			constants.stateSlot = 0;
			constants.moduleStateCount = 2;
			constants.flags = 4 | 16 | 64;
			constants.maskRange = Vector4(0, 1, 0, std::bit_cast<float>(1u));
			constants.imageSize = Vector4(2, 2, 0.5f, 1);
			constants.textureSizeOffset = Vector4(2, 2, 0, 1);
			subMesh.world = Matrix4x4::MakeTranslateMatrix(Vector3(2, 0, 0));
		}

		// ローカルRTVの破棄前にTextureの遅延回収を終える
		~SourceFixture() {

			image.Destroy();
			fixture.Finish();
		}

		// 読戻しでframeが進んでも同じ入力を再公開する
		void Dispatch(const PipelineState& pipeline, uint32_t operation) {

			vertices.Upload(vertexData); previous.Upload(vertexData);
			indices.Upload(indexData); colors.Upload(colorData);
			const std::vector<uint32_t> identifiers(vertexData.size(), 0);
			subMeshIDs.Upload(identifiers);
			const std::array<ParticleSourceSubMeshGPU, 1> transforms{subMesh};
			subMeshes.Upload(transforms);
			const std::array<float, 2> weights{4, 5};
			distribution.Upload(weights);
			const std::array<ParticleGPUPhase, 1> phase{};
			const std::array<ParticleGPUInstruction, 1> instruction{};
			const std::array<ParticleParentGPU, 1> parent{};
			phases.Upload(phase); instructions.Upload(instruction); parents.Upload(parent);
			states.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			auto* commands = fixture.commands.GetCommandList();
			commands->SetComputeRootSignature(pipeline.GetRootSignature());
			commands->SetPipelineState(pipeline.GetComputePipeline());
			const auto values = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), constants);
			if (const auto* binding = pipeline.FindBindingByName("ParticleGPUSourceConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(commands, binding, values.gpuAddress);
			}
			const uint32_t test[]{operation, count, 723, 0};
			const auto action = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), test);
			if (const auto* binding = pipeline.FindBindingByName("SourceTestConstants", ShaderBindingKind::CBV)) {
				RootBindingCommand::SetComputeCBV(commands, binding, action.gpuAddress);
			}
			const auto srv = [&](const char* name, const auto& buffer) {
				if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV)) {
					RootBindingCommand::SetComputeSRV(commands, binding, buffer.GetGPUAddress(), buffer.GetGPUHandle());
				}
			};
			srv("gGPUSourceVertices", vertices); srv("gGPUSourcePreviousVertices", previous);
			srv("gGPUSourceIndices", indices); srv("gGPUSourceVertexSubMeshes", subMeshIDs);
			srv("gGPUSourceSubMeshes", subMeshes); srv("gGPUSourceDistribution", distribution);
			srv("gGPUSourceVertexColors", colors); srv("gGPUPhases", phases);
			srv("gGPUInstructions", instructions); srv("gGPUParents", parents);
			for (const char* name : {"gGPUSourceColor", "gGPUSourceDepth", "gGPUSourceMask", "gGPUSourcePreviousDepth"}) {
				if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV)) {
					RootBindingCommand::SetComputeSRV(commands, binding, 0, image.GetSRVGPUHandle());
				}
			}
			if (const auto* binding = pipeline.FindBindingByName("gGPUModuleStates", ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(commands, binding, states.GetGPUAddress(), states.GetUAVGPUHandle());
			}
			if (const auto* binding = pipeline.FindBindingByName("gTestParticles", ShaderBindingKind::UAV)) {
				RootBindingCommand::SetComputeUAV(commands, binding, particles.GetGPUAddress(), particles.GetUAVGPUHandle());
			}
			commands->Dispatch((count + 255) / 256, 1, 1);
			fixture.commands.UAVBarrierAll();
		}

		// 検証用の属性だけを確定後に読む
		std::vector<ParticleAttributesGPU> Read() {

			particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = fixture.ReadResource(particles.GetResource(), count * sizeof(ParticleAttributesGPU));
			std::vector<ParticleAttributesGPU> result(count);
			std::memcpy(result.data(), bytes.data(), bytes.size());
			return result;
		}
	};
}

bool NEMTests::CheckParticleGPUSourceSpawn(ParticleGPUFixture& fixture) {

	TestDirectory directory("ParticleGPUSourceShader");
	const auto path = directory.GetPath() / "sourceTest.CS.hlsl";
	std::ofstream stream(path);
	const auto root = RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Particle/GPU");
	stream << "#include \"" << Algorithm::PathToUTF8(root / "particleGPUShared.hlsli") << "\"\n";
	stream << R"(
StructuredBuffer<ParticleGPUPhase> gGPUPhases : register(t2);
StructuredBuffer<ParticleGPUInstruction> gGPUInstructions : register(t0);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t3);
RWStructuredBuffer<float4> gGPUModuleStates : register(u2);
)";
	stream << "#include \"" << Algorithm::PathToUTF8(root / "particleGPUSource.hlsli") << "\"\n";
	stream << R"(
cbuffer SourceTestConstants : register(b1,space2) { uint operation; uint count; uint testSeed; uint pad; };
RWStructuredBuffer<ParticleAttributesGPU> gTestParticles : register(u0,space6);
[numthreads(256,1,1)]
void main(uint index : SV_DispatchThreadID) {
    if(index >= count) return;
    ParticleAttributesGPU particle = (ParticleAttributesGPU)0;
    if(operation != 0) particle = gTestParticles[index];
    if(operation == 0) {
        particle.alive = 1; particle.color = 1; particle.velocityLifetime = float4(0,0,1,1);
        uint random = ParticleHash(testSeed ^ index);
        ParticleSourceSpawn(particle,index,index,random);
    } else { ParticleSourceAttach(particle,index); ParticleSourceAttach(particle,index); }
    ParticleSourceApplyColor(particle,index); ParticleSourceApplyColor(particle,index);
    gTestParticles[index] = particle;
})";
	stream.close();
	const auto shaderPath = Algorithm::PathToUTF8(std::filesystem::absolute(path));
	auto pipeline = fixture.MakePipeline(shaderPath.c_str());
	if (!pipeline) return false;
	for (uint32_t contexts : {1u, 2u}) {
		fixture.ChangeFrameCount(contexts);
		SourceFixture input(fixture);
		input.Dispatch(*pipeline, 0);
		const auto initial = input.Read();
		uint32_t large = 0;
		float x = 0, y = 0;
		for (const auto& particle : initial) {
			if (!particle.alive || particle.color != Vector4(0.25f, 0.5f, 0.75f, 1) ||
				std::abs(particle.velocityLifetime.x - 4) > 1e-5f || std::abs(particle.velocityLifetime.y - 1) > 1e-5f ||
				std::abs(particle.velocityLifetime.z) > 1e-5f) return false;
			large += particle.positionAge.z < 0.5f ? 1u : 0u;
			x += particle.positionAge.x;
			y += particle.positionAge.y;
		}
		if (std::abs(float(large) / input.count - 0.8f) > 0.02f ||
			std::abs(x / input.count - 3.2f) > 0.04f || std::abs(y / input.count - 0.6f) > 0.03f) return false;
		input.subMesh.previousWorld = input.subMesh.world;
		input.subMesh.world = Matrix4x4::MakeTranslateMatrix(Vector3(3, 0, 0));
		input.constants.flags |= 2;
		input.constants.maskRange.w = std::bit_cast<float>(2u);
		input.Dispatch(*pipeline, 1);
		const auto attached = input.Read();
		for (uint32_t index = 0; index < input.count; ++index) {
			if (std::abs(attached[index].positionAge.x - initial[index].positionAge.x - 1) > 1e-5f ||
				attached[index].color != initial[index].color) return false;
		}
		input.constants.flags |= 8 | 32;
		input.constants.maskRange.x = 0.9f;
		input.Dispatch(*pipeline, 0);
		for (const auto& particle : input.Read()) if (particle.alive) return false;
		input.constants.kind = 4;
		input.constants.flags = 4 | 64;
		input.constants.emitterWorld = Matrix4x4::MakeTranslateMatrix(Vector3(2, 0, 0));
		input.Dispatch(*pipeline, 0);
		for (const auto& particle : input.Read()) {
			if (!particle.alive || particle.positionAge.x < 1 || particle.positionAge.x > 3 ||
				particle.positionAge.y < -1 || particle.positionAge.y > 1 || particle.color != Vector4(0.25f, 0.5f, 0.75f, 1)) return false;
		}
	}
	return fixture.CheckMessages();
}
