#pragma once

struct ID3D12Resource;

namespace NEMTests {

	class ParticleGPUFixture;
	// 未使用の先頭slotを飛ばしてGPU投影を描画する
	bool CheckParticleImpactDecalRaster(ParticleGPUFixture& fixture, ID3D12Resource* arguments);
}
