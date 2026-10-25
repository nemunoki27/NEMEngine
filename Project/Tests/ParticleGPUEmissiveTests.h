#pragma once

namespace Engine {

	class PipelineState;
}

namespace NEMTests {

	class ParticleGPUFixture;

	// 発光色の参照と独立色をCPU評価と照合する
	bool CheckParticleEmissive(ParticleGPUFixture& fixture, const Engine::PipelineState& pipeline);
}
