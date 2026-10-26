#pragma once

namespace Engine { class PipelineState; }

namespace NEMTests {

	class ParticleGPUFixture;

	// 同じ初期値からCPUとGPUの移動と回転を比較する
	bool CheckParticleGPUModuleParity(ParticleGPUFixture& fixture, const Engine::PipelineState& pipeline);
}
