#include "giTraceCommon.hlsli"
#include "giWorldRadiance.hlsli"

Texture2D<float4> gHistoryColor : register(t4);
Texture2D<float2> gHistoryMoments : register(t11);
RWTexture2D<float4> gDestColor : register(u0);
RWTexture2D<float2> gDestMoments : register(u1);


RWStructuredBuffer<uint> gGIUpdateJobs : register(u2);
RWStructuredBuffer<uint> gGIUpdateArguments : register(u3);
RWStructuredBuffer<uint2> gGIProbeUpdateFrames : register(u4);
RWStructuredBuffer<float4> gGICachedLighting : register(u5);
StructuredBuffer<uint> gGISelectedJobs : register(t12, space36);

[numthreads(8,8,1)]
void Select(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 size;
	gDestColor.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	uint2 p=dispatchID.xy;
	if(p.x>=n || p.y>=size.y) return;
	uint probe=p.y*n+p.x;
	uint slot=probe/64u,local=probe%64u;
	uint capacity=n*n*n/64u;
	uint word=(slot&1u)*2u+local/32u,bit=1u<<(local&31u);
	int4 brick=gGIProbeBricks[slot];
	uint4 live=asuint(gGIProbeBricks[capacity+(capacity+1u)/2u+slot/2u]);
	uint2 dest=uint2(p.x*6u,p.y);
	if(brick.w==0 || (live[word]&bit)==0u) {
		[unroll] for(uint face=0;face<6u;++face) gDestColor[dest+uint2(face,0)]=0;
		return;
	}
	int3 cell=brick.xyz*4+int3(local&3u,(local>>2u)&3u,local>>4u);
	uint previous=probe;
	bool valid=cacheFrame!=0u;
	if(layoutChanged!=0u) valid=valid && FindGIProbe(cell,n,true,uint(brick.w)-1u,previous);
	uint2 source=GIProbeTexel(previous,n);
	float marker=gHistoryColor.Load(int3(source,0)).a;
	valid=valid && GIProbeFaceTag(marker)==CellTag(cell);
	uint4 masks=asuint(gGIProbeBricks[capacity+slot/2u]);
	bool changed=lightingChanged!=0 || (masks[word]&bit)!=0;
	uint ageMultiplier=cacheConverged==1u ? 16u : cacheConverged==2u ? 4u : 1u;
	uint period=max(changed ? updatePeriod/2u : updatePeriod*ageMultiplier,1u);
	period=max((period*2u)>>updateSpeed,1u);
	bool update=!valid || (GIWorldHash(cell)+cacheFrame)%period==0;
	if(update) {
		uint revision=any(gMaterialPaintScene[0].xy!=0u) ? 0u : directRevision;
		bool reuse=valid && previous==probe && revision!=0u && gGIProbeUpdateFrames[probe].y==revision;
		gGIProbeUpdateFrames[probe]=uint2(cacheFrame,revision);
		uint index;
		InterlockedAdd(gGIUpdateArguments[0],1u,index);
		gGIUpdateJobs[index]=probe | (previous<<15u) | (uint(valid && !changed)<<30u) | (uint(reuse)<<31u);
	} else {
		uint age=cacheFrame-gGIProbeUpdateFrames[probe].x;
		// 更新後の2フレームで両方の履歴をそろえる
		if(age>2u && copyAllMoments==0u) return;
		[unroll] for(uint face=0;face<6u;++face) {
			uint2 f=uint2(face,0);
			float4 history=gHistoryColor.Load(int3(source+f,0));
			gDestColor[dest+f]=float4(history.rgb,-abs(history.a));
			// 直前に更新した面だけを反対側の履歴へ同期する
			if(marker>0 || copyAllMoments!=0u) {
				[unroll] for(uint i=0;i<4u;++i) {
					uint2 offset=uint2(i&1u,i>>1u);
					gDestMoments[(dest+f)*2u+offset]=gHistoryMoments.Load(int3((source+f)*2u+offset,0));
				}
			}
		}
	}
}
void TraceFace(uint first,uint stride,float3 origin,float spacing,uint face,bool splitDirect,bool reuseDirect,
	out float3 sum,out float3 directSum,out float4 depthSum,out float4 squareSum,out float4 weights,out float backfaceWeight,out float totalWeight) {
	sum=0;
	directSum=0;
	depthSum=0;
	squareSum=0;
	weights=0;
	backfaceWeight=0;
	totalWeight=0;
	uint count=max(rayCount,1u);
	for(uint i=first;i<count;i+=stride) {
		float2 uv=float2((i+0.5f)/count,frac(float(reversebits(i))*2.3283064365386963e-10f+0.5f/count));
		float2 st=uv*2-1;
		float weight=pow(1+dot(st,st),-1.5f);
		float3 dir=EnvironmentCubeDirection(face,uv);
		RayDesc ray={origin,0.0001f*spacing,dir,kGITraceDistance};
		GIQuery query;
		TraceGI(query,ray,false);
		float3 value=0,direct=0;
		float projectedDistance=2.0f;
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) {
			if(!query.CommittedTriangleFrontFace()) backfaceWeight+=weight;
			projectedDistance=min(projectedDistance,query.CommittedRayT()*abs(dir[face/2u])/spacing);
			value=SurfaceBounce(ResolveGISurface(query,ray),gHistoryColor,gHistoryMoments,true,!reuseDirect,direct);
		} else if(!reuseDirect) direct=EnvironmentGI(dir);
		if(splitDirect) directSum+=weight*direct;
		else value+=direct;
		uint quadrant=uint(uv.x>=0.5f)+2u*uint(uv.y>=0.5f);
		depthSum[quadrant]+=weight*projectedDistance;
		squareSum[quadrant]+=weight*projectedDistance*projectedDistance;
		weights[quadrant]+=weight;
		sum+=weight*value;
		totalWeight+=weight;
	}
}


groupshared float3 sharedColor[32],sharedDirect[32];
groupshared float4 sharedDepth[32],sharedSquare[32],sharedWeights[32];
groupshared float2 sharedFaceWeights[32];

[numthreads(32,1,1)]
void main(uint3 groupID : SV_GroupID,uint lane : SV_GroupIndex) {
	uint job=gGISelectedJobs[groupID.x];
	uint probe=job&32767u,previous=(job>>15u)&32767u,face=groupID.y;
	uint2 size;
	gDestColor.GetDimensions(size.x,size.y);
	uint n=GridDimension(size),local=probe%64u;
	int4 brick=gGIProbeBricks[probe/64u];
	int3 cell=brick.xyz*4+int3(local&3u,(local>>2u)&3u,local>>4u);
	float spacing=GILevelSpacing(n,uint(brick.w)-1u);
	float3 origin=(float3(cell)+0.5f)*spacing;
	bool splitDirect=directRevision!=0u && !any(gMaterialPaintScene[0].xy!=0u);
	uint address=probe*6u+face;
	// プローブ側の完全な世代に加え、実際に記録した面の世代も照合する
	uint tagBits=0x3f800000u|(directRevision&0x7fffffu);
	float4 cachedDirect=0;
	bool reuseDirect=(job>>31u)!=0u && splitDirect;
	if(reuseDirect) {
		cachedDirect=gGICachedLighting[address];
		reuseDirect=asuint(cachedDirect.w)==tagBits;
	}
	float3 sum,directSum;
	float4 depthSum,squareSum,weights;
	float backfaceWeight,totalWeight;
	TraceFace(lane,32u,origin,spacing,face,splitDirect,reuseDirect,sum,directSum,depthSum,squareSum,weights,backfaceWeight,totalWeight);
	if(WaveGetLaneCount()>=32u) {
		sum=WaveActiveSum(sum);
		if(splitDirect && !reuseDirect) directSum=WaveActiveSum(directSum);
		depthSum=WaveActiveSum(depthSum);
		squareSum=WaveActiveSum(squareSum);
		weights=WaveActiveSum(weights);
		backfaceWeight=WaveActiveSum(backfaceWeight);
		totalWeight=WaveActiveSum(totalWeight);
	} else {
		sharedColor[lane]=sum;
		sharedDirect[lane]=directSum;
		sharedDepth[lane]=depthSum;
		sharedSquare[lane]=squareSum;
		sharedWeights[lane]=weights;
		sharedFaceWeights[lane]=float2(backfaceWeight,totalWeight);
		GroupMemoryBarrierWithGroupSync();
		if(lane==0u) {
			for(uint i=1u;i<32u;++i) {
				sum+=sharedColor[i];
				directSum+=sharedDirect[i];
				depthSum+=sharedDepth[i];
				squareSum+=sharedSquare[i];
				weights+=sharedWeights[i];
				backfaceWeight+=sharedFaceWeights[i].x;
				totalWeight+=sharedFaceWeights[i].y;
			}
		}
	}
	if(lane!=0u) return;
	uint2 dest=GIProbeTexel(probe,n)+uint2(face,0);
	float tag=CellTag(cell);
	if(backfaceWeight>=0.75f*totalWeight) tag+=kGIBackfaceBit;
	[unroll] for(uint i=0;i<4u;++i) {
		uint2 offset=uint2(i&1u,i>>1u);
		gDestMoments[dest*2u+offset]=float2(depthSum[i],squareSum[i])/max(weights[i],1e-6f);
	}
	if(splitDirect) {
		if(reuseDirect) directSum=cachedDirect.rgb;
		else gGICachedLighting[address]=float4(directSum,asfloat(tagBits));
		sum+=directSum;
	}
	float3 color=sum/totalWeight;
	if((job&(1u<<30u))!=0u) color=lerp(color,gHistoryColor.Load(int3(GIProbeTexel(previous,n)+uint2(face,0),0)).rgb,0.25f);
	gDestColor[dest]=float4(min(color,60000.0f),tag);
}
