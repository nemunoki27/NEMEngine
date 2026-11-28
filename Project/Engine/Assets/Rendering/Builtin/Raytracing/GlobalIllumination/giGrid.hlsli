cbuffer GIWorldLighting : register(b0, space36) {
	uint2 giAtmosphereLightIndices;
	float giGroundHeight;
	float giInverseAtmosphereHeight;
	float4 giAtmosphereColors[2][32];
	float indirectIntensity;
	float emissiveContribution;
	uint giCoarseShift;
};

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

float GILevelSpacing(uint n,uint level) {
	uint shift=level==2u ? (giCoarseShift+1u)/2u : (level==1u ? max(giCoarseShift,2u) : 0u);
	return GridSpacing(n)*float(1u<<shift);
}

uint GILevelCount(bool hasCoarse) {
	return hasCoarse ? (giCoarseShift>=4u ? 3u : 2u) : 1u;
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
	uint hash=GIWorldHash(brick)^(level*0x9e3779b9u);
	probe=0;
	[loop]
	for(uint i=0;i<count;++i) {
		uint index=(hash+i)&(count-1u);
		int4 entry=previous ? gGIPreviousProbeLookup[index] : gGIProbeLookup[index];
		if(entry.w==0) return false;
		if((uint(entry.w)>>16u)==level && all(entry.xyz==brick)) {
			uint3 local=asuint(cell)&3u;
			probe=((uint(entry.w)&65535u)-1u)*64u+local.x+4u*(local.y+4u*local.z);
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

float2 ProbeDistanceMoments(Texture2D<float2> moments,uint2 base,uint2 size,float3 delta,uint axis) {
	uint face=axis*2u+(delta[axis]<0 ? 1u : 0u);
	float signAxis=(face&1u)!=0u ? -1.0f : 1.0f;
	float2 st=axis==0u ? float2(-signAxis*delta.z,-delta.y) :
		(axis==1u ? float2(delta.x,signAxis*delta.z) : float2(signAxis*delta.x,-delta.y));
	float2 blend=saturate(st/max(abs(delta[axis]),1e-6f)+0.5f);
	uint2 tile=2u*(base+uint2(face,0));
	float2 uv=(float2(tile)+0.5f+blend)/(2.0f*float2(size));
	return moments.SampleLevel(gSampler,uv,0);
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
		float2 moment=ProbeDistanceMoments(moments,base,size,delta,axis);
		float difference=max(0,abs(delta[axis])-moment.x-0.015f);
		float variance=max(moment.y-moment.x*moment.x,0.0001f);
		float probability=variance/(variance+difference*difference);
		visibility+=axisWeight[axis]*probability*probability*probability;
	}
	return saturate(visibility/total);
}

struct GIProbeNeighborhood {
	int3 firstBrick;
	uint4 lower;
	uint4 upper;
};

// 同じブリックにある候補点は検索表を一度だけ引く
bool LoadProbeNeighborhood(int3 first,int3 last,uint n,bool previous,uint level,out GIProbeNeighborhood neighborhood) {
	neighborhood.firstBrick=first>>2;
	neighborhood.lower=0xffffffffu;
	neighborhood.upper=0xffffffffu;
	uint3 span=uint3((last>>2)-neighborhood.firstBrick);
	uint activeAxes=span.x|(span.y<<1u)|(span.z<<2u);
	bool found=false;
	[loop]
	for(uint i=0;i<=activeAxes;++i) {
		if((i&activeAxes)!=i) continue;
		int3 brick=neighborhood.firstBrick+int3(i&1u,(i>>1u)&1u,i>>2u);
		uint base;
		if(!FindGIProbe(brick*4,n,previous,level,base)) continue;
		if(i<4u) neighborhood.lower[i]=base;
		else neighborhood.upper[i-4u]=base;
		found=true;
	}
	return found;
}

bool FindNeighborProbe(int3 cell,GIProbeNeighborhood neighborhood,out uint probe) {
	uint3 brick=uint3((cell>>2)-neighborhood.firstBrick);
	uint xy=brick.x+2u*brick.y;
	uint base=brick.z!=0u ? neighborhood.upper[xy] : neighborhood.lower[xy];
	if(base==0xffffffffu) {probe=0;return false;}
	uint3 local=asuint(cell)&3u;
	probe=base+local.x+4u*local.y+16u*local.z;
	return true;
}

float4 CachedLevelIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,
	uint level,uint2 size,uint n,GIFaceWeights weights) {
	float spacing=GILevelSpacing(n,level);
	float3 grid = position / spacing - 0.5f;
	int3 first = int3(floor(grid));
	float3 fraction = frac(grid);
	GIProbeNeighborhood neighborhood;
	if(!LoadProbeNeighborhood(first,first+1,n,previous,level,neighborhood)) return 0;
	float4 result = 0;
	[loop]
	for (uint i = 0; i < 8u; ++i) {
		int3 corner = int3(i & 1u, (i >> 1u) & 1u, i >> 2u);
		int3 cell = first + corner;
		float3 delta=float3(corner)-fraction;
		float distanceSquared=dot(delta,delta);
		float facing=distanceSquared<1e-8f ? 1.0f : smoothstep(-0.05f,0.25f,dot(N,delta)*rsqrt(distanceSquared));
		float3 axis = lerp(1.0f - fraction, fraction, float3(corner));
		float weight=axis.x*axis.y*axis.z*facing;
		if(weight<=0) continue;
		uint slot;
		if(!FindNeighborProbe(cell,neighborhood,slot)) continue;
		float3 probe;
		if (!ReadGIProbe(cache, cell, slot, size, weights, probe)) continue;
		result += (weight*ProbeVisibility(moments,slot,size,-delta))*float4(probe,1);
	}
	return float4(result.rgb/max(result.a,1e-6f),smoothstep(0,0.12f,result.a));
}

float4 SampleCachedIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,bool hasCoarse) {
	uint2 size;
	cache.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	GIFaceWeights weights=IrradianceWeights(N);
	float3 sum=0;
	float remaining=1;
	uint levels=GILevelCount(hasCoarse);
	[loop]
	for(uint i=0;i<levels && remaining>0;++i) {
		uint level=i==0 ? 0u : levels-i;
		float4 value=CachedLevelIrradiance(cache,moments,position,N,previous,level,size,n,weights);
		sum+=remaining*value.a*value.rgb;
		remaining*=1-value.a;
	}
	return float4(sum,1-remaining);
}

float3 CachedIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 N,bool previous,bool hasCoarse) {
	return SampleCachedIrradiance(cache,moments,position,N,previous,hasCoarse).rgb;
}

