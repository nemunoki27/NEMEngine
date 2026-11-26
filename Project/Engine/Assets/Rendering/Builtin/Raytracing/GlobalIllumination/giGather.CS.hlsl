#include "giTraceCommon.hlsli"
#include "giWorldRadiance.hlsli"

Texture2D<float4> gWorldCache : register(t4);
Texture2D<float2> gWorldMoments : register(t11);
Texture2D<float4> gSourcePosition : register(t5);
Texture2D<float4> gSourceNormal : register(t6);
Texture2D<uint> gSourceFlags : register(t7);
RWTexture2D<float4> gDestColor : register(u0);

[numthreads(8,8,1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 size,sourceSize;
	gDestColor.GetDimensions(size.x,size.y);
	gSourcePosition.GetDimensions(sourceSize.x,sourceSize.y);
	uint2 p=dispatchID.xy;
	if(any(p>=size)) return;
	uint2 start,extent;
	ScreenProbeTile(p,size,sourceSize,start,extent);
	uint2 center=clamp(uint2((p+0.5f)*sourceSize/size),start,start+extent-1u);
	uint2 source=center;
	if((gSourceFlags.Load(int3(source,0))&kMaterialFlagSurface)==0) {
		float best=1e30f;
		if(rayCount>=32u) {
			[loop]
			for(uint y=0;y<extent.y;++y) {
				[loop]
				for(uint x=0;x<extent.x;++x) {
					uint2 candidate=start+uint2(x,y);
					if((gSourceFlags.Load(int3(candidate,0))&kMaterialFlagSurface)==0) continue;
					float2 delta=float2(candidate)-float2(center);
					float distanceSquared=dot(delta,delta);
					if(distanceSquared<best) {source=candidate;best=distanceSquared;}
				}
			}
		}
		if(best==1e30f) {gDestColor[p]=float4(0,0,0,-1);return;}
	}
	float3 position=gSourcePosition.Load(int3(source,0)).xyz;
	float3 normal=normalize(gSourceNormal.Load(int3(source,0)).xyz*2-1);
	uint2 offset=source-start;
	gDestColor[p]=float4(GatherIrradiance(gWorldCache,gWorldMoments,position,normal),offset.x+offset.y*extent.x);
}
