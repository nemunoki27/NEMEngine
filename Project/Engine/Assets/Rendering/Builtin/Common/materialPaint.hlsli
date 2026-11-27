#ifndef NEM_MATERIAL_PAINT
#define NEM_MATERIAL_PAINT

#include "descriptorHeapCompatibility.hlsli"
#include "surfaceGraphMath.hlsli"

StructuredBuffer<uint4> gMaterialPaintScene : register(t0, space48);
StructuredBuffer<uint4> gMaterialPaintAtlas : register(t1, space48);

bool HasMaterialPaint(uint flags) {
	return (flags & 128u) != 0 || gMaterialPaintScene[0].y != 0;
}

float4x4 PaintMatrix(uint address) {
	return float4x4(asfloat(gMaterialPaintScene[address]), asfloat(gMaterialPaintScene[address+1]),
		asfloat(gMaterialPaintScene[address+2]), asfloat(gMaterialPaintScene[address+3]));
}

struct PaintFootprint {
	float3 dx;
	float3 dy;
	float mip;
};

float4 SamplePaintTexture(uint index, SamplerState samplerState, float2 uv, float2 dx, float2 dy, float mip, float4 fallback) {
	if (index == 0xffffffffu) return fallback;
	Texture2D<float4> image = NEM_TEXTURE2D(index);
#ifdef NEM_MATERIAL_PAINT_RASTER
	return image.SampleGrad(samplerState, uv, dx, dy);
#else
	return image.SampleLevel(samplerState, uv, mip);
#endif
}

float4 SamplePaintProjection(uint index, SamplerState samplerState, float3 p, PaintFootprint f, float3 weights, float4 fallback) {
	if (index == 0xffffffffu) return fallback;
	return SamplePaintTexture(index,samplerState,p.zy,f.dx.zy,f.dy.zy,f.mip,fallback)*weights.x +
		SamplePaintTexture(index,samplerState,p.xz,f.dx.xz,f.dy.xz,f.mip,fallback)*weights.y +
		SamplePaintTexture(index,samplerState,p.xy,f.dx.xy,f.dy.xy,f.mip,fallback)*weights.z;
}

float2 PaintBarycentrics(float3 delta, float3 a, float3 b) {
	float aa = dot(a,a), bb = dot(b,b), ab = dot(a,b);
	float det = aa*bb-ab*ab;
	return det > 0 ? float2(dot(delta,a)*bb-dot(delta,b)*ab, dot(delta,b)*aa-dot(delta,a)*ab)/det : 0;
}

// 頂点共有とメッシュレットの並べ替えを元の三角形へ戻す
bool ReadPaintMask(uint4 atlas, uint4 header, uint primitive, uint firstTriangle, float3 worldPosition,
	PaintFootprint footprint, SamplerState samplerState, out float4 mask, out uint4 surfaceData) {
	mask=1;
	surfaceData=uint4(0xffffffffu,0,0,0);
	if (header.x==0xffffffffu) return true;
	uint triangleIndex = primitive + firstTriangle;
	if ((primitive & 0x80000000u)!=0) {
		uint lane=primitive & 0x7fffffffu;
		if (lane>=atlas.w) return false;
		triangleIndex=gMaterialPaintAtlas[atlas.z+lane/4][lane%4];
	}
	if (triangleIndex>=atlas.y) return false;
	uint tri=atlas.x+triangleIndex*4;
	float4 a=asfloat(gMaterialPaintAtlas[tri]);
	float4 b=asfloat(gMaterialPaintAtlas[tri+1]);
	uint4 c=gMaterialPaintAtlas[tri+2];
	float4 axes=asfloat(gMaterialPaintAtlas[tri+3]);
	if (c.w>=header.z) return false;
	uint surface=header.y+c.w*5;
	if (!gMaterialPaintScene[surface].y) return false;
	float4x4 toLocal=PaintMatrix(surface+1);
	float3 pos=mul(float4(worldPosition,1),toLocal).xyz;
	float2 bary=PaintBarycentrics(pos-a.xyz,b.xyz,asfloat(c.xyz));
	float2 uv=float2(a.w,b.w)+bary.x*axes.xy+bary.y*axes.zw;
	float2 bx=PaintBarycentrics(mul(footprint.dx,(float3x3)toLocal),b.xyz,asfloat(c.xyz));
	float2 by=PaintBarycentrics(mul(footprint.dy,(float3x3)toLocal),b.xyz,asfloat(c.xyz));
	mask=SamplePaintTexture(header.x,samplerState,uv,bx.x*axes.xy+bx.y*axes.zw,
		by.x*axes.xy+by.y*axes.zw,footprint.mip,0);
	surfaceData=gMaterialPaintScene[surface];
	return true;
}

struct PaintGraphSurface {
	float3 color;
	float3 normal;
	float metallic;
	float roughness;
	float ao;
	float3 emissive;
};

PaintGraphSurface EvaluatePaintGraph(uint offset, uint count, float2 uv, float2 dx, float2 dy, float mip,
	float3 position, float3 normal, float3 tangent, float3 bitangent, SamplerState samplerState) {
	PaintGraphSurface result = {1.0f.xxx,normal,0,0.5f,1,0.0f.xxx};
	float4 values[64];
	values[0]=0; values[1]=1; values[2]=float4(uv,0,1); values[3]=0.5f;
	[loop] for (uint i=0; i<min(count,60u); ++i) {
		uint2 command=gMaterialPaintScene[offset+i*2].xy;
		float4 v=asfloat(gMaterialPaintScene[offset+i*2+1]);
		uint inputs=command.y;
		float4 a=values[inputs&63u], b=values[(inputs>>8)&63u];
		float4 c=values[(inputs>>16)&63u], d=values[(inputs>>24)&63u];
		switch (command.x) {
		case 0: case 1: break;
		case 2: v=values[2]; break;
		case 3: v=float4(position,1); break;
		case 4: v=float4(normal,1); break;
		case 5: v=SamplePaintTexture(asuint(a.x),samplerState,b.xy,dx,dy,mip,v); break;
		case 8: {
			float3 mapped=a.xyz*2-1;
			v=float4(normalize(tangent*mapped.x+bitangent*mapped.y+normal*mapped.z),1);
			break;
		}
		case 40:
			switch (uint(v.x)) {
			case 0: result.color=a.rgb; break;
			case 1: result.normal=a.xyz; break;
			case 2: result.metallic=a.x; break;
			case 3: result.roughness=a.x; break;
			case 4: result.ao=a.x; break;
			case 5: result.emissive=a.rgb; break;
			}
			break;
		case 41: {
			float3 clock=asfloat(gMaterialPaintScene[gMaterialPaintScene[0].z].xyz);
			uint port=uint(v.x);
			v=port==0 ? clock.x : port==1 ? sin(clock.x) : port==2 ? cos(clock.x) : port==3 ? clock.y : clock.z;
			break;
		}
		default: v=SurfaceGraphMath(command.x,a,b,c,d,v); break;
		}
		values[i+4]=v;
	}
	return result;
}

void ApplyMaterialPaintSurface(uint2 entity, uint primitive, uint firstTriangle, float3 worldPosition,
	float3 geometryNormal, float2 baseUV, float2 baseDX, float2 baseDY, PaintFootprint footprint, SamplerState samplerState,
	bool evaluateBase, float3 baseTangent, float3 baseBitangent, float3 vertexColor,
	inout float3 color, inout float3 normal, inout float roughness, inout float metallic, inout float ao, inout float3 emissive) {
	uint count = gMaterialPaintScene[0].x;
	uint low=0, high=count;
	while (low < high) {
		uint middle = (low+high)/2;
		if (gMaterialPaintScene[1+middle].x < entity.x) low=middle+1; else high=middle;
	}
	if (low>=count) return;
	uint4 entry=gMaterialPaintScene[1+low];
	if (any(entry.xy!=entity)) return;
	uint r=entry.z;
	uint4 header=gMaterialPaintScene[r+1];
	float4 mask;
	uint4 surfaceData;
	if (!ReadPaintMask(gMaterialPaintScene[r],header,primitive,firstTriangle,worldPosition,footprint,samplerState,mask,surfaceData)) return;
	uint baseHeight=surfaceData.x;
	if (evaluateBase && surfaceData.w) {
		PaintGraphSurface base=EvaluatePaintGraph(surfaceData.z,surfaceData.w,baseUV,baseDX,baseDY,footprint.mip,
			worldPosition,geometryNormal,baseTangent,baseBitangent,samplerState);
		color=base.color*vertexColor;
		normal=dot(base.normal,base.normal)>1e-8f ? normalize(base.normal) : geometryNormal;
		roughness=clamp(base.roughness,0.04f,1);
		metallic=saturate(base.metallic);
		ao=saturate(base.ao);
		emissive=max(0,base.emissive);
	}
	float4x4 projection=PaintMatrix(r+2);
	float3x3 projectionToWorld=(float3x3)PaintMatrix(r+6);
	float3 n=normalize(mul(projectionToWorld,geometryNormal));
	float3 weights=pow(abs(n),4);
	weights/=max(dot(weights,1.0f.xxx),1e-8f);
	float3 p=mul(float4(worldPosition,1),projection).xyz;
	float3 dx=mul(footprint.dx,(float3x3)projection), dy=mul(footprint.dy,(float3x3)projection);
	float height=0.5f;
	bool haveHeight=baseHeight!=0xffffffffu;
	if (haveHeight) height=SamplePaintTexture(baseHeight,samplerState,baseUV,baseDX,baseDY,footprint.mip,0.5f.xxxx).r;
	for (uint i=0; i<header.w; ++i) {
		uint layer=r+10+i*7;
		uint4 tex=gMaterialPaintScene[layer], props=gMaterialPaintScene[layer+1];
		float4 tint=asfloat(gMaterialPaintScene[layer+2]), factors=asfloat(gMaterialPaintScene[layer+3]);
		float4 scale=asfloat(gMaterialPaintScene[layer+4]);
		float w=mask[props.w & 3u];
		if (w<=0) continue;
		float3 coord=p*scale.x;
		PaintFootprint f=footprint;
		f.dx=dx*scale.x; f.dy=dy*scale.x;
		bool useHeight=props.y!=0xffffffffu;
		float nextHeight=useHeight ? saturate(scale.y*SamplePaintProjection(props.y,samplerState,coord,f,weights,1).r) : 0.5f;
		if (haveHeight && useHeight && scale.z>0)
			w=saturate(w+(nextHeight-height)*scale.z*4*w*(1-w));
		height=lerp(height,nextHeight,w);
		haveHeight=haveHeight || useHeight;
		uint channels=props.z;
		uint4 extra=gMaterialPaintScene[layer+6];
		if (extra.w) {
			PaintGraphSurface sample=(PaintGraphSurface)0;
			[loop] for (uint axis=0; axis<(extra.x!=0 ? 3u : 1u); ++axis) {
				float axisWeight = extra.x!=0 ? weights[axis] : 1.0f;
				if (axisWeight<1e-5f) continue;
				float3 U=axis==0 ? float3(0,0,1) : float3(1,0,0);
				float3 V=axis==1 ? float3(0,0,1) : float3(0,1,0);
				float3 N=normalize(geometryNormal);
				float3 T=normalize(mul(U,projectionToWorld));
				T-=N*dot(T,N);
				T=dot(T,T)>1e-8f ? normalize(T) : normalize(cross(N,abs(N.y)<0.99f ? float3(0,1,0) : float3(1,0,0)));
				float3 B=cross(N,T);
				if (dot(B,mul(V,projectionToWorld))<0) B=-B;
				PaintGraphSurface plane=EvaluatePaintGraph(extra.z,extra.w,float2(dot(coord,U),dot(coord,V)),
					float2(dot(f.dx,U),dot(f.dx,V)),float2(dot(f.dy,U),dot(f.dy,V)),f.mip,
					worldPosition,N,T,B,samplerState);
				sample.color+=plane.color*axisWeight;
				sample.normal+=plane.normal*axisWeight;
				sample.roughness+=plane.roughness*axisWeight;
				sample.metallic+=plane.metallic*axisWeight;
				sample.ao+=plane.ao*axisWeight;
				sample.emissive+=plane.emissive*axisWeight;
			}
			if (channels & 1u) color=lerp(color,sample.color,w);
			if ((channels & 2u) && dot(sample.normal,sample.normal)>1e-8f) normal=normalize(lerp(normal,normalize(sample.normal),w));
			if (channels & 4u) roughness=lerp(roughness,clamp(sample.roughness,0.04f,1),w);
			if (channels & 8u) metallic=lerp(metallic,saturate(sample.metallic),w);
			if (channels & 16u) ao=lerp(ao,saturate(sample.ao),w);
			if (channels & 32u) emissive=lerp(emissive,max(0,sample.emissive),w);
			continue;
		}
		float4 emission=asfloat(gMaterialPaintScene[layer+5]);
		if (channels & 32u) emissive=lerp(emissive,emission.rgb*emission.a*SamplePaintProjection(extra.x,samplerState,coord,f,weights,1).rgb,w);
		float4 metalRough=SamplePaintProjection(extra.y,samplerState,coord,f,weights,1);
		if (channels & 1u) color=lerp(color,tint.rgb*SamplePaintProjection(tex.x,samplerState,coord,f,weights,1).rgb,w);
		if (channels & 2u) {
			float3 nx=SamplePaintTexture(tex.y,samplerState,coord.zy,f.dx.zy,f.dy.zy,f.mip,float4(.5,.5,1,1)).xyz*2-1;
			float3 ny=SamplePaintTexture(tex.y,samplerState,coord.xz,f.dx.xz,f.dy.xz,f.mip,float4(.5,.5,1,1)).xyz*2-1;
			float3 nz=SamplePaintTexture(tex.y,samplerState,coord.xy,f.dx.xy,f.dy.xy,f.mip,float4(.5,.5,1,1)).xyz*2-1;
			float3 slope=nx.x*float3(0,0,1)*weights.x+nx.y*float3(0,1,0)*weights.x+
				ny.x*float3(1,0,0)*weights.y+ny.y*float3(0,0,1)*weights.y+
				nz.x*float3(1,0,0)*weights.z+nz.y*float3(0,1,0)*weights.z;
			float3 pn=normalize(n+(slope-dot(slope,n)*n)*factors.x);
			normal=normalize(lerp(normal,normalize(mul((float3x3)projection,pn)),w));
		}
		if (channels & 4u) roughness=lerp(roughness,max(0.04f,saturate(factors.y*metalRough.g*SamplePaintProjection(tex.z,samplerState,coord,f,weights,1).r)),w);
		if (channels & 8u) metallic=lerp(metallic,saturate(factors.z*metalRough.b*SamplePaintProjection(tex.w,samplerState,coord,f,weights,1).r),w);
		if (channels & 16u) ao=lerp(ao,saturate(factors.w*SamplePaintProjection(props.x,samplerState,coord,f,weights,1).r),w);
	}
}

void ApplyMaterialPaint(uint2 entity, uint primitive, uint firstTriangle, float3 worldPosition,
	float3 geometryNormal, float2 baseUV, float2 baseDX, float2 baseDY, PaintFootprint footprint, SamplerState samplerState,
	inout float3 color, inout float3 normal, inout float roughness, inout float metallic, inout float ao, inout float3 emissive) {
	ApplyMaterialPaintSurface(entity,primitive,firstTriangle,worldPosition,geometryNormal,baseUV,baseDX,baseDY,footprint,
		samplerState,false,0,0,1,color,normal,roughness,metallic,ao,emissive);
}
#endif
