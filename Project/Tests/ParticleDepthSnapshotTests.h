#pragma once

namespace NEMTests {

	class ParticleGPUFixture;
	// 書き込み深度と背景深度の分離を実GPUで確認する
	bool CheckParticleDepthSnapshot(ParticleGPUFixture& fixture);
}
