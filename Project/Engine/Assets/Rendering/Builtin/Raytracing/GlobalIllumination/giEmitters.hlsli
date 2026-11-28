struct GIEmitter {
	float4x4 world;
	float4 bounds;
	uint instance;
	uint geometry;
	uint triangleCount;
	uint flags;
};
StructuredBuffer<GIEmitter> gGIEmitters : register(t3,space35);
void ResolveHitPrimitiveSurface(SubMeshShaderData subMesh, MeshVertex v0, MeshVertex v1, MeshVertex v2,
	float3 bary, float2 localUV, out float2 uv, out float4 vertexColor) {

	uv = mul(float4(localUV, 0.0f, 1.0f), subMesh.uvMatrix).xy;
	vertexColor = 1.0f.xxxx;
	if (subMesh.primitiveShape == 0u) return;

	uint offset = subMesh.primitiveShapeOffset;
	float4 shape = asfloat(gRaytracingMaterialParameters.Load4(offset));
	if (subMesh.primitiveShape == 1u) {
		float2 position = v0.position.xy * bary.x + v1.position.xy * bary.y + v2.position.xy * bary.z;
		float4 basis = float4(subMesh.uvMatrix[0].xy, subMesh.uvMatrix[1].xy);
		uv = ResolvePrimitivePixelUV(uv, float4(position, localUV), shape, basis);
	} else {
		PrimitiveInstance instance = (PrimitiveInstance)0;
		instance.shapeParams0 = shape;
		instance.shapeParams1 = asfloat(gRaytracingMaterialParameters.Load4(offset + 16u));
		instance.topColor = asfloat(gRaytracingMaterialParameters.Load4(offset + 32u));
		instance.centerColor = asfloat(gRaytracingMaterialParameters.Load4(offset + 48u));
		instance.bottomColor = asfloat(gRaytracingMaterialParameters.Load4(offset + 64u));
		vertexColor = ResolvePrimitiveVertexColor(v0.position.xyz, instance) * bary.x +
			ResolvePrimitiveVertexColor(v1.position.xyz, instance) * bary.y +
			ResolvePrimitiveVertexColor(v2.position.xyz, instance) * bary.z;
	}
}


float4 SampleHitTexture(uint textureIndex, HexTilingData hex,
	float mipLevel, float4 fallbackValue, bool normalMap = false) {

	if (textureIndex == kNoTexture) {
		return fallbackValue;
	}
	Texture2D<float4> texture =
		ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
	return SampleHexLevel(texture, gSampler, hex, mipLevel, normalMap);
}


float GIUniform(uint seed) {
	return float(GIHash(seed) >> 8) / 16777216.0f;
}

void TriangleSurfaceData(RaytracingInstanceShaderData instance,RaytracingGeometryShaderData geometry,
	uint primitiveIndex,float2 baryUV,SubMeshShaderData material,out MeshVertex a,out MeshVertex b,out MeshVertex c,
	out float2 uv,out float4 color) {
	StructuredBuffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.indexDescriptorIndex)];
	StructuredBuffer<MeshVertex> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexDescriptorIndex)];
	uint offset = geometry.indexOffset + primitiveIndex * 3;
	a = vertices[instance.vertexOffset + indices[offset]];
	b = vertices[instance.vertexOffset + indices[offset + 1]];
	c = vertices[instance.vertexOffset + indices[offset + 2]];
	float3 bary = float3(1 - baryUV.x - baryUV.y, baryUV);
	ResolveHitPrimitiveSurface(material, a, b, c, bary, a.uv * bary.x + b.uv * bary.y + c.uv * bary.z, uv, color);
}

void TriangleData(uint instanceID, uint geometryIndex, uint primitiveIndex, float2 baryUV,
	out MeshVertex a, out MeshVertex b, out MeshVertex c, out SubMeshShaderData material, out float2 uv, out float4 color) {
	RaytracingInstanceShaderData instance = gRaytracingSceneInstances[instanceID];
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + geometryIndex];
	material = gRaytracingSubMeshes[geometry.subMeshDataIndex];
	TriangleSurfaceData(instance,geometry,primitiveIndex,baryUV,material,a,b,c,uv,color);
}

bool PassesHitAlpha(RaytracingInstanceShaderData instance,RaytracingGeometryShaderData geometry,
	SubMeshShaderData mat,uint primitive,float2 bary) {
	float alpha=mat.importedBaseColor.a*mat.color.a;
	float cutoff=float(mat._materialPad&65535u)/65535.0f;
	if(mat.primitiveShape==0u && mat.baseColorTextureIndex==kNoTexture && mat.opacityTextureIndex==kNoTexture)
		return alpha>=cutoff;
	MeshVertex a,b,c;
	float2 uv;
	float4 color;
	TriangleSurfaceData(instance,geometry,primitive,bary,mat,a,b,c,uv,color);
	HexTilingData hex=BuildHexTiling(uv,mat.hexTiling);
	alpha*=color.a;
	alpha*=SampleHitTexture(mat.baseColorTextureIndex,hex,0,1.0f.xxxx).a;
	alpha*=SampleHitTexture(mat.opacityTextureIndex,hex,0,1.0f.xxxx).r;
	return alpha>=cutoff;
}

#define GIQuery RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
void TraceGICandidates(inout GIQuery query, RayDesc ray, bool anyHit, uint filterIndex, uint flags) {
	query.TraceRayInline(gSceneTLAS, flags, anyHit ? 1u : 4u, ray);
	while (query.Proceed()) {
		if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
		if (filterIndex!=0xFFFFFFFFu && IsLightShadowInstanceExcluded(filterIndex,query.CandidateInstanceID())) continue;
		RaytracingInstanceShaderData instance = gRaytracingSceneInstances[query.CandidateInstanceID()];
		if (allLayersVisible==0u && ((instance.renderFlags >> 8) & cullingMask) == 0) continue;
		RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + query.CandidateGeometryIndex()];
		SubMeshShaderData mat = gRaytracingSubMeshes[geometry.subMeshDataIndex];
		uint mode = mat._materialPad >> 16;
		if (mode == 3) continue;
		if (mode == 2 && !PassesHitAlpha(instance,geometry,mat,query.CandidatePrimitiveIndex(),query.CandidateTriangleBarycentrics())) continue;
		query.CommitNonOpaqueTriangleHit();
		if (anyHit) query.Abort();
	}
}

void TraceGI(inout GIQuery query, RayDesc ray, bool anyHit, uint filterIndex=0xFFFFFFFFu, bool visibilityOnly=false) {
	uint flags=anyHit || visibilityOnly ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE;
	bool filtered=HasLightShadowExclusions(filterIndex);
	// 標準の不透明面はハードウェアで確定し、対象レイヤー外の面を通る場合だけ再走査する
	[loop]
	for(uint pass=0; pass<2u; ++pass) {
		TraceGICandidates(query,ray,anyHit,filtered ? filterIndex : 0xFFFFFFFFu,
			flags|(filtered ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE));
		if(allLayersVisible!=0u || filtered || query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT) break;
		if(((gRaytracingSceneInstances[query.CommittedInstanceID()].renderFlags>>8u)&cullingMask)!=0u) break;
		filtered=true;
	}
}

bool EmitsTowards(GIEmitter emitter,float3 edge1,float3 edge2,float3 toLight) {
	if((emitter.flags&6u)==0u) return true;
	float facing=dot(cross(edge1,edge2),toLight);
	return (emitter.flags&2u)!=0u ? facing<0 : facing>0;
}

float3 EmissionAtHit(GIQuery query,GIEmitter emitter,float3 direction,bool checkFace) {
	MeshVertex a,b,c;
	SubMeshShaderData mat;
	float2 uv;
	float4 color;
	TriangleData(query.CommittedInstanceID(),query.CommittedGeometryIndex(),query.CommittedPrimitiveIndex(),
		query.CommittedTriangleBarycentrics(),a,b,c,mat,uv,color);
	if(checkFace && (emitter.flags&6u)!=0u) {
		float3x3 transform=mul((float3x3)mat.localMatrix,(float3x3)emitter.world);
		if(!EmitsTowards(emitter,mul(b.position.xyz-a.position.xyz,transform),mul(c.position.xyz-a.position.xyz,transform),direction)) return 0;
	}
	return mat.emissiveColor.rgb*mat.emissiveColor.a*SampleHitTexture(mat.emissiveTextureIndex,
		BuildHexTiling(uv,mat.hexTiling),0,1.0f.xxxx).rgb;
}

float ProjectedEdgeAngle(float3 p,float3 q,float3 normal) {
	float3 edge=cross(p,q);
	float sine=length(edge);
	return sine>1e-12f ? dot(normal,edge)*(atan2(sine,dot(p,q))/sine) : 0.0f;
}

// 受光面で切った球面三角形の余弦積分を求める
float FrontTriangleFormFactor(float3 p,float3 q,float3 r,float3 normal) {
	float integral=ProjectedEdgeAngle(p,q,normal)+ProjectedEdgeAngle(q,r,normal)+ProjectedEdgeAngle(r,p,normal);
	return abs(integral)*(0.5f/3.14159265f);
}
float TriangleFormFactor(float3 a,float3 b,float3 c,float3 normal,out float3 target) {
	float3 height=float3(dot(a,normal),dot(b,normal),dot(c,normal));
	target=0;
	if(!any(height>0)) return 0;
	if(all(height>0)) {
		target=(a+b+c)/3.0f;
		return FrontTriangleFormFactor(a,b,c,normal);
	}
	float3 vertices[3]={a,b,c};
	float3 polygon[4];
	uint count=0;
	[unroll] for(uint i=0;i<3;++i) {
		float3 p=vertices[i],q=vertices[(i+1)%3];
		float dp=height[i],dq=height[(i+1)%3];
		if(dp>0) polygon[count++]=p;
		if((dp>0)!=(dq>0)) polygon[count++]=lerp(p,q,dp/(dp-dq));
	}
	if(count<3) return 0;
	float integral=0;
	[loop] for(uint j=0;j<count;++j) {
		float3 p=polygon[j],q=polygon[(j+1)%count];
		target+=p;
		integral+=ProjectedEdgeAngle(p,q,normal);
	}
	target/=count;
	return abs(integral)*(0.5f/3.14159265f);
}

float3 SampleEmitterCone(GIEmitter emitter,float3 origin,float3 normal,uint samples,float3 axis,float extent,
	float distanceLimit,float3 basis) {
	float3 tangent=normalize(cross(basis,axis));
	float3 bitangent=cross(axis,tangent);
	float3 sum=0;
	for(uint i=0;i<samples;++i) {
		float z=1-extent*((i+0.5f)/samples);
		float phi=2.39996323f*i;
		float3 dir=z*axis+sqrt(saturate(1-z*z))*(cos(phi)*tangent+sin(phi)*bitangent);
		float cosine=dot(normal,dir);
		if(cosine<=0) continue;
		RayDesc ray={origin,0.0001f,dir,distanceLimit};
		GIQuery query;
		TraceGI(query,ray,false);
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT && query.CommittedInstanceID()==emitter.instance &&
			query.CommittedGeometryIndex()==emitter.geometry) sum+=EmissionAtHit(query,emitter,dir,true)*cosine;
	}
	return sum*(2*extent/samples);
}

float3 IntegrateEmitterCone(GIEmitter emitter,float3 origin,float3 normal,uint samples,float3 axis,float extent,float distanceLimit) {
	float blend=smoothstep(0.75f,0.95f,abs(axis.y));
	float3 sum=0;
	if(blend<1) sum+=(1-blend)*SampleEmitterCone(emitter,origin,normal,samples,axis,extent,distanceLimit,float3(0,1,0));
	if(blend>0) sum+=blend*SampleEmitterCone(emitter,origin,normal,samples,axis,extent,distanceLimit,float3(1,0,0));
	return sum;
}

float3 MeshEmitterIrradiance(GIEmitter emitter,float3 origin,float3 normal,uint samples) {
	float3 offset=emitter.bounds.xyz-origin;
	float radius=emitter.bounds.w;
	float distanceToCenter=length(offset);
	float distanceLimit=distanceToCenter+radius+0.002f;
	float coneWeight=smoothstep(radius,1.3f*radius+0.0001f,distanceToCenter);
	float3 sum=0;
	if(coneWeight<1) sum+=(1-coneWeight)*IntegrateEmitterCone(emitter,origin,normal,samples,normal,1,distanceLimit);
	if(coneWeight>0) {
		float sinSquared=saturate((radius/distanceToCenter)*(radius/distanceToCenter));
		float extent=sinSquared/(1+sqrt(1-sinSquared));
		sum+=coneWeight*IntegrateEmitterCone(emitter,origin,normal,samples,offset/distanceToCenter,extent,distanceLimit);
	}
	return sum;
}

float3 EmissiveTrianglePatch(GIEmitter emitter,SubMeshShaderData mat,uint primitiveIndex,float3 origin,float3 normal,
	float3 p,float3 q,float3 r) {
	float3 h=float3(dot(p,normal),dot(q,normal),dot(r,normal));
	if(!any(h>0)) return 0;
	bool front=all(h>0);
	float3 target=(p+q+r)/3.0f;
	float weight=0;
	if(!front) weight=TriangleFormFactor(p,q,r,normal,target);
	float distanceToTarget=length(target);
	if((!front && weight<=0) || distanceToTarget<0.0002f) return 0;
	GIQuery query;
	if(mat.emissiveTextureIndex==kNoTexture && (mat._materialPad>>16u)<2u) {
		RayDesc visibility={origin,0.0001f,target/distanceToTarget,distanceToTarget};
		TraceGI(query,visibility,false);
		if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT &&
			(query.CommittedInstanceID()!=emitter.instance || query.CommittedGeometryIndex()!=emitter.geometry ||
			query.CommittedPrimitiveIndex()!=primitiveIndex)) return 0;
		if(front) weight=FrontTriangleFormFactor(p,q,r,normal);
		return mat.emissiveColor.rgb*mat.emissiveColor.a*weight;
	}
	RayDesc ray={origin,0.0001f,target/distanceToTarget,distanceToTarget+0.002f};
	TraceGI(query,ray,false);
	if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT || query.CommittedInstanceID()!=emitter.instance ||
		query.CommittedGeometryIndex()!=emitter.geometry || query.CommittedPrimitiveIndex()!=primitiveIndex) return 0;
	float3 emission=mat.emissiveTextureIndex==kNoTexture ? mat.emissiveColor.rgb*mat.emissiveColor.a : EmissionAtHit(query,emitter,ray.Direction,false);
	if(front) weight=FrontTriangleFormFactor(p,q,r,normal);
	return emission*weight;
}

float BoxEdgeIntegral(float3 a,float3 b,float3 normal) {
	float3 edge=cross(a,b);
	float sine=length(edge);
	return dot(edge,normal)*atan2(sine,dot(a,b))/max(sine,1e-12f);
}

float VisibleBoxPatch(float3 origin,float3 normal,float3 p,float3 u,float3 v) {
	float3 q=p+u,r=p+u+v,s=p+v,target;
	float4 facing=float4(dot(p,normal),dot(q,normal),dot(r,normal),dot(s,normal));
	if(all(facing<=0)) return 0;
	bool front=all(facing>=0);
	float weight=0;
	target=(p+r)*0.5f;
	if(!front) {
		float3 a,b;
		float wa=TriangleFormFactor(p,q,s,normal,a);
		float wb=TriangleFormFactor(q,r,s,normal,b);
		weight=wa+wb;
		target=(wa*a+wb*b)/max(weight,1e-8f);
	}
	float distanceToFace=length(target);
	if((!front && weight<=0) || distanceToFace<0.0002f) return 0;
	float bias=max(0.0001f,distanceToFace*0.000002f);
	RayDesc ray={origin,0.0001f,target/distanceToFace,max(0.0001f,distanceToFace-bias)};
	GIQuery visibility;
	TraceGI(visibility,ray,false,0xFFFFFFFFu,true);
	if(visibility.CommittedStatus()==COMMITTED_TRIANGLE_HIT) return 0;
	if(front) weight=abs(BoxEdgeIntegral(p,q,normal)+BoxEdgeIntegral(q,r,normal)+
		BoxEdgeIntegral(r,s,normal)+BoxEdgeIntegral(s,p,normal))*(0.5f/3.14159265f);
	return weight;
}

float BoxFaceQuadrature(float3 faceCenter,float3 u,float3 v,float3 normal,float projectedArea) {
	// 受光面の上に収まる面を二次のガウス求積で積分する
	u*=0.577350269f;
	v*=0.577350269f;
	const float4 x=float4(-1,-1,1,1),y=float4(-1,1,-1,1);
	float4 squared=dot(faceCenter,faceCenter)+dot(u,u)+dot(v,v)+
		2*(x*dot(faceCenter,u)+y*dot(faceCenter,v)+(x*y)*dot(u,v));
	float4 cosine=dot(normal,faceCenter)+x*dot(normal,u)+y*dot(normal,v);
	return (projectedArea/3.14159265f)*dot(max(cosine,0),rcp(max(squared*squared,1e-12f)));
}

float3 BoxEmitterIrradiance(GIEmitter emitter,RaytracingInstanceShaderData instance,SubMeshShaderData mat,
	float3 origin,float3 normal,bool secondary) {
	StructuredBuffer<MeshVertex> vertices=ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexDescriptorIndex)];
	// GenerateCubeの正負の対角から、ピボットを含む箱の中心と半辺を復元する
	float3 upper=vertices[instance.vertexOffset+3u].position.xyz;
	float3 lower=vertices[instance.vertexOffset+5u].position.xyz;
	float3 size=(upper-lower)*0.5f;
	float3 center=mul(float4((upper+lower)*0.5f,1),emitter.world).xyz-origin;
	float3 axis[3]={emitter.world[0].xyz*size.x,emitter.world[1].xyz*size.y,emitter.world[2].xyz*size.z};
	float3 normalSpan=float3(dot(normal,axis[0]),dot(normal,axis[1]),dot(normal,axis[2]));
	float height=dot(normal,center),extent=dot(abs(normalSpan),1.0f.xxx);
	if(height+extent<=0) return 0;
	float3 area[3]={cross(axis[1],axis[2]),cross(axis[2],axis[0]),cross(axis[0],axis[1])};
	float volume=dot(area[0],axis[0]);
	// GenerateCubeの頂点順は内向きなので、負スケールによる反転も含めて外側の発光を判定する
	if((emitter.flags&6u)!=0u && ((emitter.flags&4u)!=0u)!=(volume>0)) return 0;
	float3 faces=-float3(dot(center,area[0]),dot(center,area[1]),dot(center,area[2]));
	float3 projected=abs(faces)-abs(volume);
	if(volume==0 || !any(projected>0)) return 0;
	float distanceToCenter=length(center);
	float farWeight=1-smoothstep(0.08f,0.16f,emitter.bounds.w/max(distanceToCenter,1e-8f));
	farWeight*=smoothstep(extent,1.25f*extent+0.000001f,height);
	float farVisibility=farWeight;
	if(farWeight>0) {
		// 中心へ向かうレイを手前の面で止め、自身を遮蔽物に含めない
		float nearDistance=distanceToCenter*(1-abs(volume)/max(max(abs(faces.x),abs(faces.y)),abs(faces.z)));
		float bias=max(0.0001f,nearDistance*0.000002f);
		RayDesc ray={origin,0.0001f,center/distanceToCenter,max(0.0001f,nearDistance-bias)};
		GIQuery visibility;
		TraceGI(visibility,ray,false,0xFFFFFFFFu,true);
		if(visibility.CommittedStatus()==COMMITTED_TRIANGLE_HIT) farVisibility=0;
	}
	if(farVisibility==0 && farWeight==1) return 0;
	float weight=0;
	[loop]
	for(uint i=0;i<3u;++i) {
		if(projected[i]<=0) continue;
		float3 faceCenter=center+sign(faces[i])*sign(volume)*axis[i];
		float3 u=axis[(i+1u)%3u],v=axis[(i+2u)%3u];
		if(farVisibility>0) weight+=farVisibility*BoxFaceQuadrature(faceCenter,u,v,normal,projected[i]);
		if(farWeight==1) continue;
		float3 p=faceCenter-u-v;
		u*=2; v*=2;
		float angle=max(dot(u,u),dot(v,v))/max(dot(faceCenter,faceCenter),1e-8f);
		float detail=secondary ? 0.0f : smoothstep(0.08f,0.30f,angle);
		if(detail<1) weight+=(1-farWeight)*(1-detail)*VisibleBoxPatch(origin,normal,p,u,v);
		if(detail>0) {
			uint divisions=clamp(rayCount/8u,1u,4u);
			u/=divisions; v/=divisions;
			for(uint y=0;y<divisions;++y)
				for(uint x=0;x<divisions;++x)
					weight+=(1-farWeight)*detail*VisibleBoxPatch(origin,normal,p+x*u+y*v,u,v);
		}
	}
	return weight*mat.emissiveColor.rgb*mat.emissiveColor.a;
}

float3 GIEmitterIrradiance(float3 position,float3 normal,bool secondary) {
	float3 sum=0;
	float3 origin=position+normal*0.001f;
	for(uint e=0;e<emitterCount;++e) {
		GIEmitter emitter=gGIEmitters[e];
		float plane=dot(emitter.bounds.xyz-origin,normal);
		if(emitter.triangleCount==0) {
			float3 extent=float3(emitter.world[0][0],emitter.world[1][1],emitter.world[2][2]);
			if((emitter.flags&1u)==0 && plane+dot(extent,abs(normal))<=0) e=emitter.instance-1u;
			continue;
		}
		if((emitter.flags&1u)==0 && plane+emitter.bounds.w<=0) continue;
		if(emitter.triangleCount>24u && (emitter.flags&1u)==0) {
			sum+=MeshEmitterIrradiance(emitter,origin,normal,secondary ? 4u : clamp(rayCount*2u,16u,96u));
			continue;
		}
		if(emitter.triangleCount==12u && (emitter.flags&1u)==0 &&
			distance(origin,emitter.bounds.xyz)>emitter.bounds.w) {
			RaytracingInstanceShaderData instance=gRaytracingSceneInstances[emitter.instance];
			if(instance._pad!=0u) {
				uint material=gRaytracingGeometries[instance.geometryDataOffset+emitter.geometry].subMeshDataIndex;
				SubMeshShaderData mat=gRaytracingSubMeshes[material];
				if(mat.emissiveTextureIndex==kNoTexture) {
					sum+=BoxEmitterIrradiance(emitter,instance,mat,origin,normal,secondary);
					continue;
				}
			}
		}
		uint count=emitter.triangleCount;
		if(count>24u) count=min(count,secondary ? 4u : max(16u,rayCount*2u));
		for(uint sampleIndex=0;sampleIndex<count;++sampleIndex) {
			uint t=sampleIndex;
			float scale=1;
			if(count<emitter.triangleCount) {
				uint first=(uint)(float(sampleIndex)*emitter.triangleCount/count);
				uint end=sampleIndex+1==count ? emitter.triangleCount : (uint)(float(sampleIndex+1)*emitter.triangleCount/count);
				scale=float(end-first);
				t=first+min((uint)(GIUniform(first)*scale),end-first-1u);
			}
			MeshVertex a,b,c;
			SubMeshShaderData mat;
			float2 uv;
			float4 color;
			TriangleData(emitter.instance,emitter.geometry,t,1.0f.xx/3,a,b,c,mat,uv,color);
			float3 p=mul(mul(float4(a.position.xyz,1),mat.localMatrix),emitter.world).xyz-origin;
			float3 q=mul(mul(float4(b.position.xyz,1),mat.localMatrix),emitter.world).xyz-origin;
			float3 r=mul(mul(float4(c.position.xyz,1),mat.localMatrix),emitter.world).xyz-origin;
			if(!EmitsTowards(emitter,q-p,r-p,p)) continue;
			if(max(dot(normal,p),max(dot(normal,q),dot(normal,r)))<=0) continue;
			uint divisions=secondary || count<emitter.triangleCount ? 1u : clamp(rayCount/8u,1u,4u);
			// 面が占める角度に応じて影の境界だけを細かく積分する
			float3 center=(p+q+r)/3.0f;
			float span=max(dot(q-p,q-p),max(dot(r-p,r-p),dot(r-q,r-q)))/max(dot(center,center),1e-8f);
			float refinement=smoothstep(0.08f,0.30f,span);
			if(divisions==1u || refinement<1.0f)
				sum+=scale*(divisions==1u ? 1.0f : 1.0f-refinement)*EmissiveTrianglePatch(emitter,mat,t,origin,normal,p,q,r);
			if(divisions==1u || refinement==0.0f) continue;
			scale*=refinement;
			float3 u=(q-p)/divisions,v=(r-p)/divisions;
			for(uint j=0;j<divisions;++j) {
				for(uint i=0;i<divisions-j;++i) {
					float3 a0=p+i*u+j*v;
					sum+=scale*EmissiveTrianglePatch(emitter,mat,t,origin,normal,a0,a0+u,a0+v);
					if(i+j+1<divisions)
						sum+=scale*EmissiveTrianglePatch(emitter,mat,t,origin,normal,a0+u+v,a0+v,a0+u);
				}
			}
		}
	}
	return sum;
}

