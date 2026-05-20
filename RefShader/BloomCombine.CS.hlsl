
//============================================================================*/
//	BloomCombine.CS
//============================================================================*/

RWTexture2D<float4> gOutputTexture : register(u0);
Texture2D<float4> gOriginalTexture : register(t0);
Texture2D<float4> gBloomTexture : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
	
	float4 original = gOriginalTexture.Load(int3(DTid.xy, 0));
	float4 bloom = gBloomTexture.Load(int3(DTid.xy, 0));

	// ‰ÁZ‡¬
	gOutputTexture[DTid.xy] = original + bloom;
	// ƒ¿’l‚ğ1.0f‚ÉŒÅ’è‚·‚é
	gOutputTexture[DTid.xy].a = 1.0f;
}