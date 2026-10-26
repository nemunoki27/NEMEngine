#pragma once

namespace NEMTests {

	class ParticleGPUFixture;
	// 本番mainで衝突から次回の二次発生まで確認する
	bool CheckParticleGPUExtendedIntegration(ParticleGPUFixture& fixture);
}
