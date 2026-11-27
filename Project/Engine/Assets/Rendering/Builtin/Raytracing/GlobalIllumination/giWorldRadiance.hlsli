#include "../../Common/materialPaint.hlsli"

static const float kGITraceDistance = 100000.0f;

cbuffer GIWorldLighting : register(b0, space36) {
	uint2 giAtmosphereLightIndices;
	float giGroundHeight;
	float giInverseAtmosphereHeight;
	float4 giAtmosphereColors[2][32];
	float indirectIntensity;
	float emissiveContribution;
};

float3 GIAltitudeLightColor(uint index, float height, float3 color) {
	if(all(index!=giAtmosphereLightIndices)) return color;
	uint body=index==giAtmosphereLightIndices.x ? 0u : 1u;
	float t=31.0f*sqrt(saturate((height-giGroundHeight)*giInverseAtmosphereHeight));
	uint lower=min(uint(t),30u);
	return lerp(giAtmosphereColors[body][lower].rgb,giAtmosphereColors[body][lower+1u].rgb,t-lower);
}

struct GISurface {
	float3 position;
	float3 normal;
	float3 diffuse;
	float3 paintEmission;
	float ambientOcclusion;
	uint flags;
};



GISurface ResolveGISurface(GIQuery query, RayDesc ray) {
	GISurface hit=(GISurface)0;
	MeshVertex a,b,c;
	SubMeshShaderData mat;
	float2 uv;
	float4 color;
	float2 bary = query.CommittedTriangleBarycentrics();
	TriangleData(query.CommittedInstanceID(),query.CommittedGeometryIndex(),query.CommittedPrimitiveIndex(),bary,a,b,c,mat,uv,color);
	float3 N = a.normal * (1-bary.x-bary.y) + b.normal * bary.x + c.normal * bary.y;
	if (dot(N,N) < 1e-12f) N = cross(b.position.xyz-a.position.xyz,c.position.xyz-a.position.xyz);
	N = mul(float4(N,0), mat.localNormalMatrix).xyz;
	N = mul(N,(float3x3)query.CommittedWorldToObject3x4());
	hit.normal = dot(N,N) > 1e-12f ? normalize(N) : -ray.Direction;
	if (dot(hit.normal,ray.Direction)>0) hit.normal = -hit.normal;
	hit.position = ray.Origin + ray.Direction * query.CommittedRayT();
	HexTilingData hex = BuildHexTiling(uv,mat.hexTiling);
	float mip = log2(1 + query.CommittedRayT()*0.02f);
	float metallic = saturate(mat.metallic * SampleHitTexture(mat.metallicTextureIndex,hex,mip,1.0f.xxxx).r *
		SampleHitTexture(mat.metallicRoughnessTextureIndex,hex,mip,1.0f.xxxx).b);
	hit.flags = gRaytracingSceneInstances[query.CommittedInstanceID()].renderFlags;
	hit.diffuse = saturate(mat.color.rgb * mat.importedBaseColor.rgb * color.rgb *
		SampleHitTexture(mat.baseColorTextureIndex,hex,mip,1.0f.xxxx).rgb);
	hit.ambientOcclusion = saturate(SampleHitTexture(mat.occlusionTextureIndex,hex,mip,1.0f.xxxx).r);
	if (HasMaterialPaint(hit.flags)) {
		RaytracingInstanceShaderData instance = gRaytracingSceneInstances[query.CommittedInstanceID()];
		RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset+query.CommittedGeometryIndex()];
		PaintFootprint f = {0.0f.xxx,0.0f.xxx,mip};
		float roughness = mat.roughness;
		float3 geometryNormal = hit.normal;
		float3 T=a.tangent*(1-bary.x-bary.y)+b.tangent*bary.x+c.tangent*bary.y;
		T=mul((float3x3)query.CommittedObjectToWorld3x4(),mul(T,(float3x3)mat.localMatrix));
		T-=geometryNormal*dot(T,geometryNormal);
		T=dot(T,T)>1e-8f ? normalize(T) : normalize(cross(geometryNormal,abs(geometryNormal.y)<0.99f ? float3(0,1,0) : float3(1,0,0)));
		float signT=a.tangentSign*(1-bary.x-bary.y)+b.tangentSign*bary.x+c.tangentSign*bary.y;
		float orientation=determinant((float3x3)query.CommittedObjectToWorld3x4())<0 ? -1 : 1;
		float3 B=cross(geometryNormal,T)*(signT<0 ? -1 : 1)*mat.localOrientationSign*orientation;
		ApplyMaterialPaintSurface(uint2(instance.entityIndex,instance.entityGeneration),query.CommittedPrimitiveIndex(),
			geometry.indexOffset/3,hit.position,geometryNormal,uv,0,0,f,gSampler,true,T,B,color.rgb,hit.diffuse,hit.normal,
			roughness,metallic,hit.ambientOcclusion,hit.paintEmission);
	}
	if ((hit.flags & kMaterialFlagLighting)!=0u) hit.diffuse = DiffuseGIReflectance(hit.diffuse,metallic);
	
	return hit;
}

float3 EnvironmentGI(float3 direction) {
	if (gHasSkybox == 0 || gSkyboxCubemapIndex == kNoTexture) return ambientIntensity.xxx;
	TextureCube<float4> sky = ResourceDescriptorHeap[gSkyboxCubemapIndex];
	uint width,height,levels;
	sky.GetDimensions(0,width,height,levels);
	// 1本のレイが代表する立体角に合わせて入射放射輝度を取得する
	float mip = 0.5f * log2(max(1.0f, float(width)*width / max(1u,rayCount)));
	return sky.SampleLevel(gSampler,direction,min(mip,float(levels-1u))).rgb * gSkyboxColor.rgb * gIBLIntensity;
}

float3 VisibleLight(GISurface hit, float3 direction, float distanceLimit, float3 radiance, float strength, uint filterIndex) {
	if (!any(radiance > 0) || dot(hit.normal,direction)<=0) return 0;
	if ((hit.flags & kMaterialFlagReceiveShadow)==0 || strength<=0 || distanceLimit<0.003f) return radiance;
	RayDesc ray = {hit.position + hit.normal * 0.002f,0.001f,direction,distanceLimit-0.001f};
	GIQuery query;
	TraceGI(query,ray,true,filterIndex);
	return radiance * (query.CommittedStatus()==COMMITTED_TRIANGLE_HIT ? 1-saturate(strength) : 1);
}

float3 DirectGI(GISurface hit) {
	float3 sum=0;
	for (uint i=0;i<directionalCount;++i) {
		DirectionalLight l=gDirectionalLights[i];
		if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
		float3 L=normalize(-l.direction);
		float3 color=GIAltitudeLightColor(i,hit.position.y,l.color.rgb);
		sum+=VisibleLight(hit,L,kGITraceDistance,color*l.intensity*saturate(dot(hit.normal,L)),l.shadowStrength,l.shadowFilterIndex);
	}
	for (uint j=0;j<pointCount;++j) {
		PointLight l=gPointLights[j];
		if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
		float3 delta=l.pos-hit.position;
		float d=max(length(delta),0.001f);
		float3 L=delta/d;
		sum+=VisibleLight(hit,L,d,l.color.rgb*l.intensity*saturate(dot(hit.normal,L))*ComputeDistanceAttenuation(d,l.radius,l.decay),l.shadowStrength,l.shadowFilterIndex);
	}
	for (uint k=0;k<spotCount;++k) {
		SpotLight l=gSpotLights[k];
		if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
		float3 delta=l.pos-hit.position;
		float d=max(length(delta),0.001f);
		float3 L=delta/d;
		float cone=saturate((dot(normalize(l.direction),-L)-l.cosAngle)/max(l.cosFalloffStart-l.cosAngle,0.0001f));
		cone*=cone;
		sum+=VisibleLight(hit,L,d,l.color.rgb*l.intensity*saturate(dot(hit.normal,L))*ComputeDistanceAttenuation(d,l.distance,l.decay)*cone,l.shadowStrength,l.shadowFilterIndex);
	}
	for(uint m=0;m<rectCount;++m) {
		RectLight l=gRectLights[m];
		if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
		float3 radiance=GetRectLightRadiance(l)*ComputeRectRangeAttenuation(l,hit.position)*ComputeRectBarnAttenuation(l,hit.position);
		if(!any(radiance>0)) continue;
		float3 right=l.right*(l.sourceWidth*0.25f);
		float3 up=l.up*(l.sourceHeight*0.25f);
		float3 center=l.pos-hit.position;
		[loop]
		for(uint region=0;region<4u;++region) {
			float3 middle=center+((region&1u)!=0 ? right : -right)+((region&2u)!=0 ? up : -up);
			float3 a=middle-right-up,b=middle+right-up,c=middle+right+up,d=middle-right+up;
			float3 first,second;
			float w1=TriangleFormFactor(a,b,c,hit.normal,first);
			float w2=TriangleFormFactor(a,c,d,hit.normal,second);
			float form=w1+w2;
			if(form<=0) continue;
			middle=(w1*first+w2*second)/form;
			float distanceToLight=max(length(middle),0.001f);
			sum+=VisibleLight(hit,middle/distanceToLight,distanceToLight,radiance*(kEnvironmentPI*form),l.shadowStrength,l.shadowFilterIndex);
		}
	}
	return sum / 3.14159265f;
}

float3 EmissiveIrradiance(float3 position,float3 normal,bool secondary) {
	if(emissiveContribution<=0) return 0;
	return GIEmitterIrradiance(position,normal,secondary)*emissiveContribution;
}

float3 SurfaceRadiance(GISurface hit,Texture2D<float4> cache,Texture2D<float2> moments,bool previous) {
	float3 emission=hit.paintEmission*emissiveContribution;
	if ((hit.flags & kMaterialFlagLighting)==0u || !any(hit.diffuse)) return hit.diffuse+emission;
	float3 irradiance = DirectGI(hit);
	if ((hit.flags & kMaterialFlagReceiveIBL)!=0u && hit.ambientOcclusion>0.0f) {
		irradiance += hit.ambientOcclusion*(EmissiveIrradiance(hit.position,hit.normal,true)+
			CachedIrradiance(cache,moments,hit.position+0.002f*hit.normal,hit.normal,previous,hasCoarseBricks!=0u));
	}
	return hit.diffuse*irradiance + emission;
}

float3 TraceDistantIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal) {
	float3 sum=0;
	float totalWeight=0;
	uint count=max(rayCount*2u,8u);
	for(uint i=0;i<count;++i) {
		float y=1-2*(i+0.5f)/count;
		float r=sqrt(saturate(1-y*y));
		float phi=2.39996323f*i;
		float3 dir=float3(r*cos(phi),y,r*sin(phi));
		float weight=dot(normal,dir);
		if(weight<=0) continue;
		RayDesc ray={position+normal*0.002f,0.001f,dir,kGITraceDistance};
		GIQuery query;
		TraceGI(query,ray,false);
		float3 value;
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT)
			value=SurfaceRadiance(ResolveGISurface(query,ray),cache,moments,false);
		else value=EnvironmentGI(dir);
		sum+=value*weight;
		totalWeight+=weight;
	}
	return sum/max(totalWeight,0.0001f);
}

struct GIProbeNeighborhood {
	int3 firstBrick;
	uint4 lower;
	uint4 upper;
};

// 同じブリックにある候補点は検索表を一度だけ引く
bool LoadProbeNeighborhood(int3 first,int3 last,uint n,uint level,out GIProbeNeighborhood neighborhood) {
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
		if(!FindGIProbe(brick*4,n,false,level,base)) continue;
		if(i<4u) neighborhood.lower[i]=base;
		else neighborhood.upper[i-4u]=base;
		found=true;
	}
	return found;
}

float4 ReadVisibleIrradiance(Texture2D<float4> cache,int3 cell,GIProbeNeighborhood neighborhood,uint2 size,
	GIFaceWeights weights,float3 delta,float3 position,float3 normal,float wrap) {
	float3 direction=delta/max(length(delta),0.0001f);
	float facing=saturate(dot(normal,direction)+wrap);
	if(facing<=0) return 0;
	uint3 brick=uint3((cell>>2)-neighborhood.firstBrick);
	uint xy=brick.x+2u*brick.y;
	uint base=brick.z!=0u ? neighborhood.upper[xy] : neighborhood.lower[xy];
	if(base==0xffffffffu) return 0;
	uint3 local=asuint(cell)&3u;
	uint probe=base+local.x+4u*local.y+16u*local.z;
	float3 bias=normal*0.002f;
	float3 segment=delta-bias;
	float lengthToCell=length(segment);
	if(lengthToCell>0.006f) {
		RayDesc ray={position+bias,0.0001f,segment/lengthToCell,lengthToCell};
		GIQuery query;
		TraceGI(query,ray,false,0xFFFFFFFFu,true);
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) return 0;
	}
	float3 radiance;
	if(!ReadGIProbe(cache,cell,probe,size,weights,radiance)) return 0;
	return float4(radiance,1.0f)*facing;
}

float NeighborWeight(float3 delta) {
	float kernel=saturate(1.0f-dot(delta,delta)*0.25f);
	return kernel*kernel*kernel;
}

float4 GatherLevelIrradiance(Texture2D<float4> cache,float3 position,float3 normal,uint level,uint n,uint2 size,
	GIFaceWeights weights) {
	float spacing=GridSpacing(n)*(level==0u ? 1.0f : 4.0f);
	float3 grid=position/spacing-0.5f;
	int3 first=int3(floor(grid));
	float3 fraction=frac(grid);
	float4 close=0;
	float4 wide=0;
	bool high=rayCount>=32u;
	GIProbeNeighborhood neighborhood;
	if(!LoadProbeNeighborhood(first-(high ? 1 : 0),first+(high ? 2 : 1),n,level,neighborhood)) return 0;
	for(uint i=0;i<8;++i) {
		int3 corner=int3(i&1,(i>>1)&1,i>>2);
		float3 axisWeights=lerp(1-fraction,fraction,float3(corner));
		float3 delta=float3(corner)-fraction;
		float4 probe=ReadVisibleIrradiance(cache,first+corner,neighborhood,size,weights,delta*spacing,position,normal,high ? 0.0f : 0.1f);
		close+=probe*(axisWeights.x*axisWeights.y*axisWeights.z);
		if(high) wide+=probe*NeighborWeight(delta);
	}
	float confidence=smoothstep(0,0.12f,close.a);
	float3 indirect=close.rgb/max(close.a,0.0001f);
	// 壁際で近い格子点が遮蔽される場合、半径2セル内の可視点を使う
	if(high && confidence<1.0f) {
		[loop]
		for(uint i=0;i<64u;++i) {
			int3 corner=int3(i&3,(i>>2)&3,i>>4)-1;
			if(all(corner>=0) && all(corner<2)) continue;
			float3 delta=float3(corner)-fraction;
			float weight=NeighborWeight(delta);
			if(weight<=0) continue;
			wide+=weight*ReadVisibleIrradiance(cache,first+corner,neighborhood,size,weights,delta*spacing,position,normal,0);
		}
		float outerConfidence=smoothstep(0,0.025f,wide.a)*(1-confidence);
		float combined=confidence+outerConfidence;
		indirect=(confidence*indirect+outerConfidence*wide.rgb/max(wide.a,0.0001f))/max(combined,0.0001f);
		confidence=combined;
	}
	return float4(indirect,confidence);
}

float3 GatherIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal) {
	uint2 size;
	cache.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	GIFaceWeights weights=IrradianceWeights(normal);
	float4 fine=GatherLevelIrradiance(cache,position,normal,0,n,size,weights);
	float3 indirect=fine.rgb;
	if(fine.a<1.0f) {
		float4 coarse=0;
		if(hasCoarseBricks!=0u) coarse=GatherLevelIrradiance(cache,position,normal,1,n,size,weights);
		float3 fallback=coarse.rgb;
		if(coarse.a<1.0f) fallback=lerp(TraceDistantIrradiance(cache,moments,position,normal),fallback,coarse.a);
		indirect=lerp(fallback,indirect,fine.a);
	}
	return indirect+EmissiveIrradiance(position,normal,false);
}

// 各スクリーンプローブが担当する画素範囲
void ScreenProbeTile(uint2 index,uint2 probeSize,uint2 sourceSize,out uint2 start,out uint2 extent) {
	start=index*sourceSize/probeSize;
	extent=max(1u,(index+1u)*sourceSize/probeSize-start);
}

