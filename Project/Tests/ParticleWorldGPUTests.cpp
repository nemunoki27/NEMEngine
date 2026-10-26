#include "ParticleWorldTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Particle/ParticleGPUWorldResources.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <cmath>
#include <cstring>

using namespace Engine;

bool NEMTests::CheckParticleWorldGPU(ParticleGPUFixture& fixture) {

	auto pipeline = fixture.MakePipeline("../../../../../../Tests/Shaders/particleWorldValidation.CS.hlsl");
	if (!pipeline) return false;
	ParticleGPUWorldResources resources;
	resources.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement);
	StructuredRWBuffer<Vector4> results;
	results.Init(fixture.device.Get(), &fixture.descriptors);
	results.EnsureCapacity(22);
	auto snapshot = std::make_shared<ParticleGPUWorldSnapshot>();
	ParticleGPUCollider sphere;
	sphere.metadata[0] = sphere.metadata[1] = 1;
	sphere.centerRadius.w = 1;
	snapshot->colliders.push_back(sphere);
	BuildParticleGPUCollisionBVH(*snapshot, 1);
	snapshot->constants.counts[2] = static_cast<uint32_t>(snapshot->collisionNodes.size());
	ParticleGPUHeightmap terrain;
	terrain.size = Vector4(4, 1, 4, 0);
	terrain.grid[0] = terrain.grid[1] = 2;
	terrain.grid[3] = 1;
	snapshot->heightmaps.push_back(terrain);
	terrain.localToWorld = Matrix4x4::MakeTranslateMatrix(Vector3(0, 2, 0));
	terrain.worldToLocal = Matrix4x4::MakeTranslateMatrix(Vector3(0, -2, 0));
	terrain.velocity.y = 2;
	snapshot->heightmaps.push_back(terrain);
	snapshot->heightSamples.resize(4);
	ParticleGPUVectorField field;
	field.grid[0] = field.grid[1] = field.grid[2] = 2;
	field.parameters.z = 2;
	snapshot->vectorFields.push_back(field);
	snapshot->vectorSamples.resize(8, Vector4(1, 2, 3, 0));
	snapshot->splineRanges.push_back({0, 2, 2, 0});
	snapshot->splineSamples.push_back({Vector4(0, 0, 0, 0), Vector4(1, 0, 0, 0)});
	snapshot->splineSamples.push_back({Vector4(2, 0, 0, 2), Vector4(1, 0, 0, 0)});
	for (uint32_t count : {1u, 2u}) {
		fixture.ChangeFrameCount(count);
		for (uint32_t frame = 0; frame < count + 1; ++frame) {
			resources.Upload(snapshot);
			results.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			auto* commands = fixture.commands.GetCommandList();
			commands->SetComputeRootSignature(pipeline->GetRootSignature());
			commands->SetPipelineState(pipeline->GetComputePipeline());
			resources.Bind(commands, *pipeline);
			RootBindingCommand::SetComputeUAV(commands, pipeline->FindBindingByName("gWorldResults", ShaderBindingKind::UAV),
				results.GetGPUAddress(), results.GetUAVGPUHandle());
			commands->Dispatch(1, 1, 1);
			fixture.commands.UAVBarrierAll();
			results.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = fixture.ReadResource(results.GetResource(), sizeof(Vector4) * 22);
			Vector4 actual[22];
			std::memcpy(actual, bytes.data(), sizeof(actual));
			const float times[]{0.4375f, 0.25f, 0.25f, 1.0f / 3.0f, 0.125f, 0.4375f, 0.25f};
			for (uint32_t i = 0; i < 7; ++i)
				if (actual[i * 2 + 1].w != 1 || std::abs(actual[i * 2].x - times[i]) > 0.0005f) return false;
			if (actual[0].z < 0.99f || actual[2].y > -0.99f || actual[9].x != 2) return false;
			if (std::abs(actual[14].x + 1) > 0.0001f || actual[16] != Vector4(1, 2, 3, 1)) return false;
			if (std::abs(actual[18].x - 1.5f) > 0.0001f || actual[19].x < 0.99f) return false;
			if (std::abs(actual[20].x - 0.625f) > 0.0005f || actual[21].w != 1 || std::abs(actual[21].y - 2) > 0.0001f) return false;
		}
	}
	return fixture.CheckMessages();
}
