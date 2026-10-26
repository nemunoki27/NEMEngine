#pragma once

namespace NEMTests {

	class ParticleGPUFixture;

	// 発生経路と保存値の上限を確認する
	bool TestParticleFeedbackSettings();
	// 本番Shaderで上限と世代と残留Poolを確認する
	bool CheckParticleFeedback(ParticleGPUFixture& fixture);
}
