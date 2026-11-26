uint GIHash(uint x) {
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	return x ^ (x >> 16);
}
StructuredBuffer<int4> gGIProbeBricks : register(t0,space35);
StructuredBuffer<int4> gGIProbeLookup : register(t1,space35);
StructuredBuffer<int4> gGIPreviousProbeLookup : register(t2,space35);

// 解像度ごとのワールド格子間隔
float GridSpacing(uint n) {
	return 24.0f/float(n);
}

uint GridDimension(uint2 size) {
	return size.x/6u;
}

// プローブの6面は常に同じ行に並ぶ
uint2 GIProbeTexel(uint probe,uint n) {
	return uint2((probe&(n-1u))*6u,probe>>firstbitlow(n));
}

uint GIWorldHash(int3 cell) {
	return GIHash(asuint(cell.x)^GIHash(asuint(cell.y)^GIHash(asuint(cell.z))));
}

// ワールド格子点を現在または直前のアトラスへ対応付ける
bool FindGIProbe(int3 cell,uint n,bool previous,uint level,out uint probe) {
	uint count=(n*n*n/64u)*2u;
	int3 brick=cell>>2;
	uint hash=GIWorldHash(brick)^(level==0u ? 0u : 0x9e3779b9u);
	probe=0;
	[loop]
	for(uint i=0;i<count;++i) {
		uint index=(hash+i)&(count-1u);
		int4 entry=previous ? gGIPreviousProbeLookup[index] : gGIProbeLookup[index];
		if(entry.w==0) return false;
		if((entry.w<0)==(level!=0u) && all(entry.xyz==brick)) {
			uint3 local=asuint(cell)&3u;
			probe=(uint(abs(entry.w))-1u)*64u+local.x+4u*(local.y+4u*local.z);
			return true;
		}
	}
	return false;
}

float CellTag(int3 cell) {
	return float(1u+(GIWorldHash(cell)&511u));
}

static const uint kGIBackfaceBit=1024u;
uint GIProbeFaceTag(float marker) {
	return uint(abs(marker)) & (kGIBackfaceBit-1u);
}

struct GIFaceWeights {
	float3 positive;
	float3 negative;
};

// 各面を四分割し、余弦と立体角の積をGauss積分する
float3 FaceCosineSum(float3 n, float u, float v) {
	float3 a = n.yzx * u;
	float3 b = n.zxy * v;
	return max(abs(n), abs(a+b)) + max(abs(n), abs(a-b));
}

GIFaceWeights IrradianceWeights(float3 n) {
	const float inner = 0.21132487f;
	const float outer = 0.78867513f;
	const float innerWeight = 0.21068418f;
	const float mixedWeight = 0.09f;
	const float outerWeight = 0.04964640f;
	float3 even = innerWeight * FaceCosineSum(n, inner, inner) +
		mixedWeight * (FaceCosineSum(n, inner, outer) + FaceCosineSum(n, outer, inner)) +
		outerWeight * FaceCosineSum(n, outer, outer);
	float3 odd = (2.0f * (innerWeight + 2.0f * mixedWeight + outerWeight)) * n;
	GIFaceWeights weights;
	weights.positive = max(0.0f, even + odd);
	weights.negative = max(0.0f, even - odd);
	float3 total=weights.positive+weights.negative;
	float scale=rcp(max(total.x+total.y+total.z,1e-6f));
	weights.positive*=scale;
	weights.negative*=scale;
	return weights;
}

bool ReadGIProbe(Texture2D<float4> cache, int3 cell, uint probe, uint2 size, GIFaceWeights weights, out float3 value) {
	uint2 base=GIProbeTexel(probe,GridDimension(size));
	float4 first=cache.Load(int3(base,0));
	value=0;
	if(GIProbeFaceTag(first.a)!=CellTag(cell)) return false;
	bool enclosed=uint(abs(first.a))>=kGIBackfaceBit;
	value=first.rgb*weights.positive.x;
	[unroll]
	for (uint face = 1; face < 6u; ++face) {
		float4 sample=cache.Load(int3(base+uint2(face,0),0));
		enclosed=enclosed && uint(abs(sample.a))>=kGIBackfaceBit;
		float weight = (face & 1u) == 0u ? weights.positive[face / 2u] : weights.negative[face / 2u];
		value+=weight*sample.rgb;
	}
	return !enclosed;
}

// 各面に射影した距離モーメントから、その面の手前にある遮蔽物を判定する
float ProbeVisibility(Texture2D<float2> moments,uint probe,uint2 size,float3 delta) {
	float3 axisWeight=delta*delta;
	axisWeight*=axisWeight;
	float total=axisWeight.x+axisWeight.y+axisWeight.z;
	if(total<1e-8f) return 1;
	uint2 base=GIProbeTexel(probe,GridDimension(size));
	float visibility=0;
	[unroll]
	for(uint axis=0;axis<3u;++axis) {
		uint face=axis*2u+(delta[axis]<0 ? 1u : 0u);
		float2 moment=moments.Load(int3(base+uint2(face,0),0));
		float difference=max(0,abs(delta[axis])-moment.x-0.015f);
		float variance=max(moment.y-moment.x*moment.x,0.0001f);
		float probability=variance/(variance+difference*difference);
		visibility+=axisWeight[axis]*probability*probability*probability;
	}
	return saturate(visibility/total);
}

float4 CachedLevelIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,
	uint level,uint2 size,uint n,GIFaceWeights weights) {
	float spacing=GridSpacing(n)*(level==0u ? 1.0f : 4.0f);
	float3 grid = position / spacing - 0.5f;
	int3 first = int3(floor(grid));
	float3 fraction = frac(grid);
	float4 result = 0;
	[loop]
	for (uint i = 0; i < 8u; ++i) {
		int3 corner = int3(i & 1u, (i >> 1u) & 1u, i >> 2u);
		int3 cell = first + corner;
		uint slot;
		if (!FindGIProbe(cell,n,previous,level,slot)) continue;
		float3 delta=float3(corner)-fraction;
		float distanceSquared=dot(delta,delta);
		float facing=distanceSquared<1e-8f ? 1.0f : smoothstep(-0.05f,0.25f,dot(N,delta)*rsqrt(distanceSquared));
		if(facing<=0) continue;
		float visibility=ProbeVisibility(moments,slot,size,-delta);
		float3 probe;
		if (!ReadGIProbe(cache, cell, slot, size, weights, probe)) continue;
		float3 axis = lerp(1.0f - fraction, fraction, float3(corner));
		result += (axis.x*axis.y*axis.z*facing*visibility)*float4(probe,1);
	}
	return float4(result.rgb/max(result.a,1e-6f),smoothstep(0,0.12f,result.a));
}

float4 SampleCachedIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,bool hasCoarse) {
	uint2 size;
	cache.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	GIFaceWeights weights=IrradianceWeights(N);
	float4 fine=CachedLevelIrradiance(cache,moments,position,N,previous,0,size,n,weights);
	if(fine.a>=1.0f) return fine;
	if(!hasCoarse) return float4(fine.rgb*fine.a,fine.a);
	float4 coarse=CachedLevelIrradiance(cache,moments,position,N,previous,1,size,n,weights);
	return lerp(float4(coarse.rgb*coarse.a,coarse.a),float4(fine.rgb,1),fine.a);
}

float3 CachedIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,bool hasCoarse) {
	return SampleCachedIrradiance(cache,moments,position,N,previous,hasCoarse).rgb;
}

