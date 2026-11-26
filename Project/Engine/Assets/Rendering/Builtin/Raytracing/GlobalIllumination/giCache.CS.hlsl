#include "giTraceCommon.hlsli"
#include "giWorldRadiance.hlsli"

Texture2D<float4> gHistoryColor : register(t4);
Texture2D<float2> gHistoryMoments : register(t11);
RWTexture2D<float4> gDestColor : register(u0);
RWTexture2D<float2> gDestMoments : register(u1);

[numthreads(8,8,1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 size;
	gDestColor.GetDimensions(size.x,size.y);
	uint2 p=dispatchID.xy;
	if(any(p>=size)) return;
	uint n=GridDimension(size);
	uint index=p.y*size.x+p.x;
	uint probe=index/6;
	if(probe>=n*n*n) {gDestColor[p]=0;gDestMoments[p]=0;return;}
	int4 brick=gGIProbeBricks[probe/64u];
	if(brick.w==0) {gDestColor[p]=0;gDestMoments[p]=0;return;}
	uint local=probe%64u;
	int3 cell=brick.xyz*4+int3(local&3u,(local>>2u)&3u,local>>4u);
	uint level=uint(brick.w)-1u;
	float spacing=GridSpacing(n)*(level==0u ? 1.0f : 4.0f);
	float3 origin=(float3(cell)+0.5f)*spacing;
	float tag=CellTag(cell);
	uint previousProbe=probe;
	bool valid=true;
	if(layoutChanged!=0u) valid=FindGIProbe(cell,n,true,level,previousProbe);
	uint face=index%6u;
	uint2 previousPixel=GIProbeTexel(previousProbe,n)+uint2(face,0);
	float4 history=valid ? gHistoryColor.Load(int3(previousPixel,0)) : 0;
	valid=valid && GIProbeFaceTag(history.a)==tag;
	bool changed=lightingChanged!=0;
	[branch]
	if(!changed && changeBounds[0].w>0) {
		[unroll]
		for(uint b=0;b<8;++b) {
			float radius=changeBounds[b].w;
			float3 delta=origin-changeBounds[b].xyz;
			changed |= radius>0 && dot(delta,delta)<=(radius+spacing)*(radius+spacing);
		}
	}
	uint ageMultiplier=cacheConverged==1u ? 16u : cacheConverged==2u ? 4u : 1u;
	uint period=max(changed ? updatePeriod/2u : updatePeriod*ageMultiplier,1u);
	period=max((period*2u)>>updateSpeed,1u);
	// 同じ格子点の六方向は同一フレームで更新する
	if(valid && (GIWorldHash(cell)+cacheFrame)%period!=0) {
		gDestColor[p]=float4(history.rgb,-abs(history.a));
		gDestMoments[p]=gHistoryMoments.Load(int3(previousPixel,0));
		return;
	}
	float3 sum=0;
	float2 moments=0;
	float backfaceWeight=0;
	float totalWeight=0;
	uint count=max(rayCount,1u);
	for(uint i=0;i<count;++i) {
		float2 uv=float2((i+0.5f)/count,frac(float(reversebits(i))*2.3283064365386963e-10f+0.5f/count));
		float2 st=uv*2-1;
		float weight=pow(1+dot(st,st),-1.5f);
		float3 dir=EnvironmentCubeDirection(face,uv);
		RayDesc ray={origin,0.0001f*spacing,dir,kGITraceDistance};
		GIQuery query;
		TraceGI(query,ray,false);
		float3 value;
		float projectedDistance=2.0f;
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) {
			if(!query.CommittedTriangleFrontFace()) backfaceWeight+=weight;
			projectedDistance=min(projectedDistance,query.CommittedRayT()*abs(dir[face/2u])/spacing);
			value=SurfaceRadiance(ResolveGISurface(query,ray),gHistoryColor,gHistoryMoments,true);
		} else value=EnvironmentGI(dir);
		moments+=weight*float2(projectedDistance,projectedDistance*projectedDistance);
		sum+=weight*value;
		totalWeight+=weight;
	}
	float3 current=sum/totalWeight;
	gDestMoments[p]=moments/totalWeight;
	if(backfaceWeight>=0.75f*totalWeight) tag+=kGIBackfaceBit;
	gDestColor[p]=float4(min(lerp(current,history.rgb,valid && !changed ? 0.25f : 0),60000.0f),tag);
}
