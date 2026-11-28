#include "../../Lighting/worldLightIndex.hlsli"
#include "../../Common/materialPaint.hlsli"

static const float kGITraceDistance = 100000.0f;

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

// 受光面をまたがない矩形では、4区画に共通する辺の積分を一度だけ求める
float3 VisibleRectLight(GISurface hit,RectLight light,float3 center,float3 right,float3 up,float3 radiance,bool aboveSurface) {
	float horizontal[6],vertical[6];
	if(aboveSurface) {
		[unroll] for(uint row=0;row<3u;++row) {
			float3 middle=center+(2.0f*row-2.0f)*up;
			horizontal[2u*row]=ProjectedEdgeAngle(middle-2.0f*right,middle,hit.normal);
			horizontal[2u*row+1u]=ProjectedEdgeAngle(middle,middle+2.0f*right,hit.normal);
		}
		[unroll] for(uint column=0;column<3u;++column) {
			float3 middle=center+(2.0f*column-2.0f)*right;
			vertical[2u*column]=ProjectedEdgeAngle(middle-2.0f*up,middle,hit.normal);
			vertical[2u*column+1u]=ProjectedEdgeAngle(middle,middle+2.0f*up,hit.normal);
		}
	}
	float3 sum=0;
	[loop] for(uint region=0;region<4u;++region) {
		uint x=region&1u,y=region>>1u;
		float3 middle=center+(x!=0u ? right : -right)+(y!=0u ? up : -up);
		float3 a=middle-right-up,c=middle+right+up;
		float form;
		if(aboveSurface) {
			float diagonal=ProjectedEdgeAngle(a,c,hit.normal);
			float first=abs(horizontal[2u*y+x]+vertical[2u*(x+1u)+y]-diagonal);
			float second=abs(diagonal-horizontal[2u*(y+1u)+x]-vertical[2u*x+y]);
			float angle=first+second;
			if(angle<=0) continue;
			middle+=(right-up)*((first-second)/(3.0f*angle));
			form=angle*(0.5f/kEnvironmentPI);
		} else {
			float3 first,second;
			float w1=TriangleFormFactor(a,middle+right-up,c,hit.normal,first);
			float w2=TriangleFormFactor(a,c,middle-right+up,hit.normal,second);
			form=w1+w2;
			if(form<=0) continue;
			middle=(w1*first+w2*second)/form;
		}
		float d=max(length(middle),0.001f);
		sum+=VisibleLight(hit,middle/d,d,radiance*(kEnvironmentPI*form),light.shadowStrength,light.shadowFilterIndex);
	}
	return sum;
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
	WorldLightCursor cursor = BeginWorldLights(hit.position);
	uint index;
	[loop]
	while (NextWorldLight(hit.position, cursor, index)) {
		if (index < pointCount) {
			PointLight l=gPointLights[index];
			if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
			float3 delta=l.pos-hit.position;
			float distanceSquared=dot(delta,delta);
			if(distanceSquared>=l.radius*l.radius || dot(delta,hit.normal)<=0) continue;
			float d=max(sqrt(distanceSquared),0.001f);
			float3 L=delta/d;
			sum+=VisibleLight(hit,L,d,l.color.rgb*l.intensity*saturate(dot(hit.normal,L))*ComputeDistanceAttenuation(d,l.radius,l.decay),l.shadowStrength,l.shadowFilterIndex);
		} else if (index < pointCount + spotCount) {
			SpotLight l=gSpotLights[index - pointCount];
			if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
			float3 delta=l.pos-hit.position;
			float distanceSquared=dot(delta,delta);
			if(distanceSquared>=l.distance*l.distance || dot(delta,hit.normal)<=0) continue;
			float d=max(sqrt(distanceSquared),0.001f);
			float3 L=delta/d;
			float cone=saturate((dot(normalize(l.direction),-L)-l.cosAngle)/max(l.cosFalloffStart-l.cosAngle,0.0001f));
			cone*=cone;
			sum+=VisibleLight(hit,L,d,l.color.rgb*l.intensity*saturate(dot(hit.normal,L))*ComputeDistanceAttenuation(d,l.distance,l.decay)*cone,l.shadowStrength,l.shadowFilterIndex);
		} else {
			RectLight l=gRectLights[index - pointCount - spotCount];
			if (((hit.flags >> 8u) & l.renderingLayerMask)==0) continue;
			float3 right=l.right*(l.sourceWidth*0.25f);
			float3 up=l.up*(l.sourceHeight*0.25f);
			float3 center=l.pos-hit.position;
			float height=dot(center,hit.normal);
			float extent=2.0f*(abs(dot(right,hit.normal))+abs(dot(up,hit.normal)));
			if(height+extent<=0.0f) continue;
			float3 radiance=GetRectLightRadiance(l)*ComputeRectRangeAttenuation(l,hit.position)*ComputeRectBarnAttenuation(l,hit.position);
			if(!any(radiance>0)) continue;
			sum+=VisibleRectLight(hit,l,center,right,up,radiance,height>extent);
		}
	}
	return sum / 3.14159265f;
}

float3 EmissiveIrradiance(float3 position,float3 normal,bool secondary) {
	if(emissiveContribution<=0) return 0;
	return GIEmitterIrradiance(position,normal,secondary)*emissiveContribution;
}

// 固定レイの一次照明と、前フレームから伝搬する間接光を分ける
float3 SurfaceBounce(GISurface hit,Texture2D<float4> cache,Texture2D<float2> moments,bool previous,bool lightSurface,out float3 direct) {
	direct=0;
	if(lightSurface) direct=hit.paintEmission*emissiveContribution;
	if((hit.flags&kMaterialFlagLighting)==0u || !any(hit.diffuse)) {
		if(lightSurface) direct+=hit.diffuse;
		return 0;
	}
	float3 irradiance=0;
	if(lightSurface) irradiance=DirectGI(hit);
	float3 bounce=0;
	if((hit.flags&kMaterialFlagReceiveIBL)!=0u && hit.ambientOcclusion>0) {
		if(lightSurface) irradiance+=hit.ambientOcclusion*EmissiveIrradiance(hit.position,hit.normal,true);
		bounce=CachedIrradiance(cache,moments,hit.position+0.002f*hit.normal,hit.normal,previous,hasCoarseBricks!=0u);
	}
	direct+=hit.diffuse*irradiance;
	return hit.diffuse*(hit.ambientOcclusion*bounce);
}

float3 SurfaceRadiance(GISurface hit,Texture2D<float4> cache,Texture2D<float2> moments,bool previous) {
	float3 direct;
	float3 bounce=SurfaceBounce(hit,cache,moments,previous,true,direct);
	return direct+bounce;
}

float4 TraceDistantSamples(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal,uint first,uint stride) {
	float3 sum=0;
	float totalWeight=0;
	uint count=max(rayCount*2u,8u);
	for(uint i=first;i<count;i+=stride) {
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
	return float4(sum,totalWeight);
}

uint GIWaveLane(uint4 mask,uint rank) {
	uint offset=0;
	if(rank>=countbits(mask.x)+countbits(mask.y)) {
		rank-=countbits(mask.x)+countbits(mask.y);
		mask.xy=mask.zw;
		offset=64;
	}
	uint bits=mask.x;
	if(rank>=countbits(bits)) {
		rank-=countbits(bits);
		bits=mask.y;
		offset+=32;
	}
	while(rank!=0) {
		bits&=bits-1;
		--rank;
	}
	return offset+firstbitlow(bits);
}

float3 TraceDistantIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal,bool needed) {
	uint pending=WaveActiveCountBits(needed);
	if(pending==0u) return 0;
	uint workers=WaveActiveCountBits(true);
	uint lane=WaveGetLaneIndex();
	uint stride=1u<<firstbithigh(workers/pending);
	uint rank=lane,leader=lane;
	bool full=workers==WaveGetLaneCount();
	uint4 active=WaveActiveBallot(true);
	if(stride>1u) {
		rank=full ? lane : WavePrefixCountBits(true);
		leader=min(WavePrefixCountBits(needed)*stride,workers-1u);
		uint owner=GIWaveLane(WaveActiveBallot(needed),min(rank/stride,pending-1u));
		position=WaveReadLaneAt(position,owner);
		normal=WaveReadLaneAt(normal,owner);
	}
	float4 sample=0;
	if(stride==1u ? needed : rank/stride<pending)
		sample=TraceDistantSamples(cache,moments,position,normal,rank%stride,stride);
	for(uint step=stride/2u;step!=0u;step>>=1u) {
		uint partner=full ? rank^step : GIWaveLane(active,min(rank^step,workers-1u));
		sample+=WaveReadLaneAt(sample,partner);
	}
	if(stride>1u) sample=WaveReadLaneAt(sample,full ? leader : GIWaveLane(active,leader));
	return needed ? sample.rgb/max(sample.a,0.0001f) : 0;
}

float4 ReadVisibleIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float spacing,int3 cell,GIProbeNeighborhood neighborhood,uint2 size,
	GIFaceWeights weights,float3 delta,float3 position,float3 normal,float wrap) {
	float3 direction=delta/max(length(delta),0.0001f);
	float facing=saturate(dot(normal,direction)+wrap);
	if(facing<=0) return 0;
	uint probe;
	if(!FindNeighborProbe(cell,neighborhood,probe)) return 0;
	float3 radiance;
	if(!ReadGIProbe(cache,cell,probe,size,weights,radiance)) return 0;
	float3 bias=normal*0.002f;
	float3 segment=delta-bias;
	float lengthToCell=length(segment);
	float3 gridDelta=-segment/spacing;
	float3 extent=abs(gridDelta);
	uint axis=extent.x>extent.y ? 0u : 1u;
	if(extent.z>extent[axis]) axis=2u;
	float2 depth=ProbeDistanceMoments(moments,GIProbeTexel(probe,GridDimension(size)),size,gridDelta,axis);
	float deviation=sqrt(max(depth.y-depth.x*depth.x,0));
	float clearance=depth.x-2.0f*deviation;
	if(lengthToCell>0.006f && extent[axis]+0.015f>=clearance) {
		RayDesc ray={position+bias,0.0001f,segment/lengthToCell,lengthToCell};
		GIQuery query;
		TraceGI(query,ray,false,0xFFFFFFFFu,true);
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) return 0;
	}
	return float4(radiance,1.0f)*facing;
}

float NeighborWeight(float3 delta) {
	float kernel=saturate(1.0f-dot(delta,delta)*0.25f);
	return kernel*kernel*kernel;
}

float4 GatherLevelIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal,uint level,uint n,uint2 size,
	GIFaceWeights weights) {
	float spacing=GILevelSpacing(n,level);
	float3 grid=position/spacing-0.5f;
	int3 first=int3(floor(grid));
	float3 fraction=frac(grid);
	float4 close=0;
	float4 wide=0;
	bool high=rayCount>=32u;
	GIProbeNeighborhood neighborhood;
	if(!LoadProbeNeighborhood(first-(high ? 1 : 0),first+(high ? 2 : 1),n,false,level,neighborhood)) return 0;
	for(uint i=0;i<8;++i) {
		int3 corner=int3(i&1,(i>>1)&1,i>>2);
		float3 axisWeights=lerp(1-fraction,fraction,float3(corner));
		float3 delta=float3(corner)-fraction;
		float4 probe=ReadVisibleIrradiance(cache,moments,spacing,first+corner,neighborhood,size,weights,delta*spacing,position,normal,high ? 0.0f : 0.1f);
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
			wide+=weight*ReadVisibleIrradiance(cache,moments,spacing,first+corner,neighborhood,size,weights,delta*spacing,position,normal,0);
		}
		float outerConfidence=smoothstep(0,0.025f,wide.a)*(1-confidence);
		float combined=confidence+outerConfidence;
		indirect=(confidence*indirect+outerConfidence*wide.rgb/max(wide.a,0.0001f))/max(combined,0.0001f);
		confidence=combined;
	}
	return float4(indirect,confidence);
}

float3 GatherProbeIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal,bool needed) {
	if(!WaveActiveAnyTrue(needed)) return 0;
	uint2 size;
	cache.GetDimensions(size.x,size.y);
	uint n=GridDimension(size);
	GIFaceWeights weights=IrradianceWeights(normal);
	float3 indirect=0;
	float remaining=1;
	uint levels=GILevelCount(hasCoarseBricks!=0u);
	[loop]
	for(uint i=0;needed && i<levels && remaining>0;++i) {
		uint level=i==0 ? 0u : levels-i;
		float4 value=GatherLevelIrradiance(cache,moments,position,normal,level,n,size,weights);
		indirect+=remaining*value.a*value.rgb;
		remaining*=1-value.a;
	}
	float3 distant=TraceDistantIrradiance(cache,moments,position,normal,needed && remaining>0);
	if(!needed) return 0;
	return indirect+remaining*distant;
}

float3 GatherIrradiance(Texture2D<float4> cache,Texture2D<float2> moments,float3 position,float3 normal,bool needed=true) {
	float3 indirect=GatherProbeIrradiance(cache,moments,position,normal,needed);
	return needed ? indirect+EmissiveIrradiance(position,normal,false) : 0;
}

// 各スクリーンプローブが担当する画素範囲
void ScreenProbeTile(uint2 index,uint2 probeSize,uint2 sourceSize,out uint2 start,out uint2 extent) {
	start=index*sourceSize/probeSize;
	extent=max(1u,(index+1u)*sourceSize/probeSize-start);
}

