#include "ParticleSourceTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Particle/Sources/ParticleMeshSampling.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <cmath>
#include <cstring>

using namespace Engine;

bool NEMTests::CheckParticleMeshSampling(ParticleGPUFixture& fixture) {

	auto prepare = fixture.MakePipeline("particleMeshSampling.CS.hlsl");
	auto scan = fixture.MakePipeline("../../Compute/inclusiveScan.CS.hlsl");
	if (!prepare || !scan) return false;
	std::vector<MeshVertex> vertices(7);
	const Vector3 positions[]{Vector3(0, 0, 0), Vector3(2, 0, 0), Vector3(0, 1, 0),
		Vector3(0, 0, 1), Vector3(4, 0, 1), Vector3(0, 2, 1), Vector3(99, 99, 99)};
	for (uint32_t i = 0; i < vertices.size(); ++i) vertices[i].position = Vector4(positions[i].x, positions[i].y, positions[i].z, 1);
	StructuredInstanceBuffer<MeshVertex> vertexBuffer;
	StructuredInstanceBuffer<uint32_t> indexBuffer, subMeshIDs;
	StructuredInstanceBuffer<ParticleSourceSubMeshGPU> subMeshBuffer;
	vertexBuffer.Init(fixture.device.Get(), &fixture.descriptors);
	indexBuffer.Init(fixture.device.Get(), &fixture.descriptors);
	subMeshIDs.Init(fixture.device.Get(), &fixture.descriptors);
	subMeshBuffer.Init(fixture.device.Get(), &fixture.descriptors);
	vertexBuffer.Upload(vertices);
	const std::vector<uint32_t> indices{0, 1, 2, 3, 4, 5}, vertexSubMeshes{0, 0, 0, 1, 1, 1, 0};
	indexBuffer.Upload(indices);
	subMeshIDs.Upload(vertexSubMeshes);
	ParticleMeshSamplingInput input;
	input.vertices = vertexBuffer.GetGPUAddress();
	input.indices = indexBuffer.GetGPUAddress();
	input.vertexSubMeshes = subMeshIDs.GetGPUAddress();
	input.vertexCount = 7;
	input.indexCount = 6;
	input.subMeshCount = 2;
	for (bool filtered : {false, true}) {
		std::vector<ParticleSourceSubMeshGPU> subMeshes(2);
		subMeshes[1].enabled = filtered ? 0u : 1u;
		subMeshBuffer.Upload(subMeshes);
		input.subMeshes = subMeshBuffer.GetGPUAddress();
		for (const auto kind : {ParticleSourceKind::MeshVertex, ParticleSourceKind::MeshEdge, ParticleSourceKind::MeshSurface}) {
			input.kind = kind;
			ParticleMeshSampling sampling;
			sampling.Init(fixture.device.Get(), fixture.descriptors, input);
			sampling.Record(fixture.device.Get(), fixture.commands, fixture.retirement, *prepare, *scan, input, fixture.constants);
			auto& distribution = sampling.GetDistribution();
			distribution.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = fixture.ReadResource(distribution.GetResource(), sampling.GetCount() * sizeof(float));
			std::vector<float> weights;
			if (kind == ParticleSourceKind::MeshVertex) weights = {1, 1, 1, filtered ? 0.0f : 1.0f,
				filtered ? 0.0f : 1.0f, filtered ? 0.0f : 1.0f, 0};
			else if (kind == ParticleSourceKind::MeshEdge) weights = {2, std::sqrt(5.0f), 1, filtered ? 0.0f : 4.0f,
				filtered ? 0.0f : std::sqrt(20.0f), filtered ? 0.0f : 2.0f};
			else weights = {1, filtered ? 0.0f : 4.0f};
			float expected = 0;
			for (uint32_t i = 0; i < sampling.GetCount(); ++i) {
				expected += weights[i];
				float actual;
				std::memcpy(&actual, bytes.data() + i * sizeof(float), sizeof(float));
				if (std::abs(actual - expected) > 0.0001f) return false;
			}
		}
	}
	return fixture.CheckMessages();
}
