#ifndef NEM_PARTICLE_CYLINDER_PROFILE
#define NEM_PARTICLE_CYLINDER_PROFILE

float SmoothParticleCylinderProfile(float t, float weight, bool pullEnd) {
	float smoothT = t * t * (3.0f - 2.0f * t);
	float power = 1.0f + saturate(weight) * 3.0f;
	return pullEnd ? 1.0f - pow(1.0f - smoothT, power) : pow(smoothT, power);
}

float ParticleCylinderRadius(float heightT, float4 parameters0, float4 parameters1) {
	if (heightT <= 0.5f) {
		float t = SmoothParticleCylinderProfile(heightT * 2.0f, parameters1.y, false);
		return lerp(parameters0.z, parameters0.y, t);
	}
	float t = SmoothParticleCylinderProfile((heightT - 0.5f) * 2.0f, parameters1.x, true);
	return lerp(parameters0.y, parameters0.x, t);
}

float4 ParticleCylinderColor(float heightT, float4 parameters1, float4 topColor, float4 centerColor, float4 bottomColor) {
	if (heightT <= 0.5f) {
		float t = SmoothParticleCylinderProfile(heightT * 2.0f, parameters1.y, false);
		return lerp(bottomColor, centerColor, t);
	}
	float t = SmoothParticleCylinderProfile((heightT - 0.5f) * 2.0f, parameters1.x, true);
	return lerp(centerColor, topColor, t);
}

#endif
