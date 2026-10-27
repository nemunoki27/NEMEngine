cbuffer GPUScanConstants : register(b0) {
	uint scanOffset;
	uint scanCount;
	uint scanParentOffset;
	uint scanOperation;
	uint scanGroupCountX;
	uint3 scanPadding;
};

RWStructuredBuffer<float> gScanValues : register(u0);
groupshared float scanGroupValues[256];

[numthreads(256, 1, 1)]
void main(uint thread : SV_GroupIndex, uint3 group : SV_GroupID) {
	uint groupIndex = group.x + group.y * scanGroupCountX;
	if (groupIndex >= (scanCount - 1u) / 256u + 1u) return;
	uint index = groupIndex * 256u + thread;
	if (scanOperation == 1u) {
		if (index < scanCount && groupIndex != 0) gScanValues[scanOffset + index] += gScanValues[scanParentOffset + groupIndex - 1];
		return;
	}
	scanGroupValues[thread] = index < scanCount ? gScanValues[scanOffset + index] : 0.0f;
	GroupMemoryBarrierWithGroupSync();
	// Group内の読み出しが揃ってから次の段を書き込む
	[unroll] for (uint shift = 1u; shift < 256u; shift <<= 1u) {
		float add = thread >= shift ? scanGroupValues[thread - shift] : 0.0f;
		GroupMemoryBarrierWithGroupSync();
		scanGroupValues[thread] += add;
		GroupMemoryBarrierWithGroupSync();
	}
	if (index < scanCount) gScanValues[scanOffset + index] = scanGroupValues[thread];
	if (thread == 255u && scanParentOffset != 0xffffffffu) gScanValues[scanParentOffset + groupIndex] = scanGroupValues[thread];
}
