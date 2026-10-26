#include "GPUInclusiveScanTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/DxObject/Buffers/GPUInclusiveScan.h>

// c++
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace Engine;

bool NEMTests::CheckGPUInclusiveScan(ParticleGPUFixture& fixture) {

	if (MakeGPUInclusiveScanPlan(0).storageCount != 0) return false;
	try {
		MakeGPUInclusiveScanPlan(UINT32_MAX);
		return false;
	} catch (const std::length_error&) {
	}
	auto pipeline = fixture.MakePipeline("../../Compute/inclusiveScan.CS.hlsl");
	if (!pipeline) return false;
	// Group境界と2次元Dispatchの境界までCPUの累積和と比較する
	for (const uint32_t count : {1u, 255u, 256u, 257u, 65536u, 65537u, 1000000u, 16777217u}) {
		const auto plan = MakeGPUInclusiveScanPlan(count);
		StructuredRWBuffer<float> values;
		values.Init(fixture.device.Get(), &fixture.descriptors);
		values.EnsureCapacity(plan.storageCount);
		StructuredInstanceBuffer<float> upload;
		upload.Init(fixture.device.Get(), &fixture.descriptors);
		std::vector<float> weights(count);
		for (uint32_t i = 0; i < count; ++i) weights[i] = i % 3 == 0 ? 0.0f : 1.0f;
		upload.Upload(weights);
		values.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
		fixture.commands.GetCommandList()->CopyBufferRegion(values.GetResource(), 0, upload.GetResource(), 0, uint64_t(count) * sizeof(float));
		RecordGPUInclusiveScan(fixture.device.Get(), fixture.commands, fixture.retirement, *pipeline, values, plan, fixture.constants);
		values.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto bytes = fixture.ReadResource(values.GetResource(), uint64_t(count) * sizeof(float));
		float expected = 0;
		for (uint32_t i = 0; i < count; ++i) {
			expected += weights[i];
			float actual;
			std::memcpy(&actual, bytes.data() + uint64_t(i) * sizeof(float), sizeof(float));
			if (actual != expected) {
				std::cerr << "GPU scan mismatch: count=" << count << " index=" << i << '\n';
				return false;
			}
		}
	}
	return fixture.CheckMessages();
}
