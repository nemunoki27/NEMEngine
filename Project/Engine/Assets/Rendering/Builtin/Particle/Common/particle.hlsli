#ifndef NEM_PARTICLE_HLSLI
#define NEM_PARTICLE_HLSLI

static const uint PARTICLE_BLEND_MULTIPLY = 3u;
static const uint PARTICLE_BLEND_SCREEN = 4u;
static const uint PARTICLE_BLEND_PREMULTIPLIED = 5u;

//============================================================================
//	Particle hlsli
//============================================================================
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float3 cameraPosition;
	float _viewPad0;
	float3 cameraForward;
	float orthographic;
};

struct ParticleGeometryData {

	float4x4 worldMatrix;
	float4 vertexColor;
	// 形状アニメーション用のパラメータと断面色
	float4 shapeParams0;
	float4 shapeParams1;
	float4 topColor;
	float4 centerColor;
	float4 bottomColor;
};

struct ParticleMaterialData {

	// 発光色と強さ、wが強さ
	float4 emissive;
	// xがアルファ棄却の閾値、yがScreen2D PlaneのV反転、zがBlendMode
	float4 materialParams;
	// フェーズマテリアルの寿命アニメーション色
	float4 materialColor;
	// カラーテクスチャのUV変換行列
	float4x4 uvMatrix;
	float4 velocityAge;
	float4 lifetimeRandom;
	float4 customData1;
	float4 customData2;
	float4 particleColor;
};

StructuredBuffer<ParticleGeometryData> gParticleGeometry : register(t1);
StructuredBuffer<ParticleMaterialData> gParticleMaterials : register(t0, space1);

struct VSOutput {

	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
	float4 vertexColor : COLOR0;
	nointerpolation uint particleIndex : PARTICLE_INDEX;
};

#include "particleCylinderProfile.hlsli"

float EvaluateParticleCylinderRadius(float heightT, ParticleGeometryData instance) {
	return ParticleCylinderRadius(heightT, instance.shapeParams0, instance.shapeParams1);
}

float4 ResolveParticleVertexColor(float3 localPosition, ParticleGeometryData instance) {
	if (instance.shapeParams1.w < 0.5f) return instance.vertexColor;
	float heightT = saturate(localPosition.y / max(abs(instance.shapeParams0.w), 0.00001f) + 0.5f);
	return instance.vertexColor * ParticleCylinderColor(heightT, instance.shapeParams1,
		instance.topColor, instance.centerColor, instance.bottomColor);
}

ParticleMaterialData GetParticleMaterial(VSOutput input) {

	if (input.particleIndex != 0xffffffffu) {
		return gParticleMaterials[input.particleIndex];
	}
	ParticleMaterialData material = (ParticleMaterialData) 0;
	return material;
}

float2 TransformParticleUV(float2 uv, ParticleMaterialData material) {

	if (material.materialParams.y != 0.0f) {
		uv.y = 1.0f - uv.y;
	}
	return mul(float4(uv, 0.0f, 1.0f), material.uvMatrix).xy;
}

float4 PrepareParticleBlendColor(float4 color, ParticleMaterialData material) {

	const uint blendMode = (uint) round(material.materialParams.z);
	const float alpha = saturate(color.a);
	if (blendMode == PARTICLE_BLEND_MULTIPLY) {
		// Multiplyは透明時に乗算係数1へ戻して描画先へ影響させない
		color.rgb = lerp(1.0f.xxx, color.rgb, alpha);
	} else if (blendMode == PARTICLE_BLEND_SCREEN ||
		blendMode == PARTICLE_BLEND_PREMULTIPLIED) {
		// ScreenとPremultipliedはブレンド前にRGBをα乗算する
		color.rgb *= alpha;
	}
	return color;
}
#endif // NEM_PARTICLE_HLSLI
