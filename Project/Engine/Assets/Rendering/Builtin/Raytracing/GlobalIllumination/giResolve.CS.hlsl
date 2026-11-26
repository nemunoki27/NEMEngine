#include "../ReflectionFilter/reflectionFilterCommon.hlsli"
#include "giTraceCommon.hlsli"
#include "giWorldRadiance.hlsli"

Texture2D<float4> gWorldCache : register(t8);
Texture2D<float2> gWorldMoments : register(t11);
Texture2D<float4> gSourceColor : register(t10);
Texture2D<float4> gSourcePosition : register(t5);
Texture2D<float4> gSourceNormal : register(t6);
Texture2D<uint> gSourceFlags : register(t7);
RWTexture2D<float4> gDestColor : register(u0);

float CacheUpdateFraction(float3 position) {
	uint2 size;
	gWorldCache.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	int3 cell=int3(floor(position/GridSpacing(n)));
	uint probe;
	if(!FindGIProbe(cell,n,false,0u,probe)) {
		cell=cell>>2;
		if(!FindGIProbe(cell,n,false,1u,probe)) return 0;
	}
	// 6面は同時に更新されるので更新印は1面だけ参照する
	float marker=gWorldCache.Load(int3(GIProbeTexel(probe,n),0)).a;
	return marker>0 && GIProbeFaceTag(marker)==CellTag(cell) ? 1.0f : 0.0f;
}

struct GIProbeSample {
	float4 radiance;
	float3 position;
	float3 normal;
	uint2 samplePixel;
};

groupshared GIProbeSample groupProbes[64];
[numthreads(8,8,1)]
void main(uint3 dispatchID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
	uint2 size,probeSize;
	gDestColor.GetDimensions(size.x,size.y);
	gSourceColor.GetDimensions(probeSize.x,probeSize.y);
	uint2 p=dispatchID.xy;
	int2 firstProbe=int2(floor((float2(groupID.xy*8)+0.5f)*probeSize/size))-1;
	uint2 lastPixel=min(groupID.xy*8+7u,size-1u);
	int2 lastProbe=int2(floor((float2(lastPixel)+0.5f)*probeSize/size))+1;
	uint2 tileSize=uint2(lastProbe-firstProbe+1);
	if(groupIndex<tileSize.x*tileSize.y) {
		int2 index=clamp(firstProbe+int2(groupIndex%tileSize.x,groupIndex/tileSize.x),int2(0,0),int2(probeSize)-1);
		GIProbeSample value;
		value.radiance=gSourceColor.Load(int3(index,0));
		uint2 start,extent;
		ScreenProbeTile(uint2(index),probeSize,size,start,extent);
		uint offset=uint(max(0,value.radiance.a));
		uint2 samplePixel=start+uint2(offset%extent.x,offset/extent.x);
		value.samplePixel=samplePixel;
		value.position=gSourcePosition.Load(int3(samplePixel,0)).xyz;
		value.normal=DecodeGBufferNormal(gSourceNormal.Load(int3(samplePixel,0)).xyz);
		groupProbes[groupIndex]=value;
	}
	GroupMemoryBarrierWithGroupSync();
	if(any(p>=size)) return;
	float2 uv=(float2(p)+0.5f)/size;
	float3 P=gSourcePosition.Load(int3(p,0)).xyz;
	float3 N=DecodeGBufferNormal(gSourceNormal.Load(int3(p,0)).xyz);
	bool surface=(gSourceFlags.Load(int3(p,0))&1u)!=0;
	float4 output=float4(0,0,0,-1);
	if(surface) {
		int2 center=int2(floor(uv*probeSize));
		float3 sum=0;
		float weightSum=0;
		float bestWeight=0;
		float4 exact=0;
		float4 viewPosition=mul(float4(P,1),cameraView);
		float clipW=abs(mul(viewPosition,cameraProjection).w);
		float2 scale=abs(float2(cameraProjection[0][0],cameraProjection[1][1]));
		float2 footprint=2*clipW/max(scale*float2(probeSize),1e-6f);
		float planeTolerance=max(1e-4f,0.125f*max(footprint.x,footprint.y));
		[unroll]
		for(int y=-1;y<=1;++y) {
			[unroll]
			for(int x=-1;x<=1;++x) {
				int2 coord=center+int2(x,y);
				int2 local=coord-firstProbe;
				GIProbeSample data=groupProbes[uint(local.y)*tileSize.x+uint(local.x)];
				float4 probe=data.radiance;
				if(probe.a<0) continue;
				float2 d=(float2(data.samplePixel)-float2(p))*float2(probeSize)/float2(size);
				if(all(data.samplePixel==p)) exact=float4(probe.rgb,1);
				float3 delta=data.position-P;
				float3 n=data.normal;
				float alignment=saturate(dot(n,N));
				float planeDistance=max(abs(dot(delta,N)),abs(dot(delta,n)));
				float square=alignment*alignment;
				float fourth=square*square;
				float w=exp2(-dot(d,d)*1.5f-planeDistance/planeTolerance)*fourth*fourth*fourth;
				sum+=probe.rgb*w;
				bestWeight=max(bestWeight,w);
				weightSum+=w;
			}
		}
		float3 irradiance=sum/max(weightSum,1e-6f);
		float confidence=smoothstep(0,0.05f,weightSum);
		// 同じ面を捉えた近傍がある限り、明暗差の大きい部分も補間できる
		if(rayCount>=32u) confidence*=smoothstep(0.01f,0.12f,bestWeight);
		if(confidence<1.0f) {
			float3 surface=exact.rgb;
			if(exact.a==0) surface=GatherIrradiance(gWorldCache,gWorldMoments,P,N);
			irradiance=lerp(surface,irradiance,confidence);
		}
		output=float4(min(irradiance*indirectIntensity,60000.0f),0);
		[branch]
		if(showCacheUpdates!=0u) output.a=CacheUpdateFraction(P);
	}
	gDestColor[p]=output;
}

