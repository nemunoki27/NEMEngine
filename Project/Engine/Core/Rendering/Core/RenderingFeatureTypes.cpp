#include "RenderingFeatureTypes.h"

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	GraphicsMeshLOD namespaceMethods
//============================================================================
bool Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
	float lod0, float lod1, float lod2) {

	return std::isfinite(lod0) &&
		std::isfinite(lod1) &&
		std::isfinite(lod2) &&
		lod0 <= kMaximumPixelThreshold &&
		lod2 >= kMinimumPixelThreshold &&
		lod0 - lod1 >= kPixelThresholdGap &&
		lod1 - lod2 >= kPixelThresholdGap;
}

std::array<float, 3> Engine::GraphicsMeshLOD::ClampPixelThresholds(
	float lod0, float lod1, float lod2) {

	if (!std::isfinite(lod0) ||
		!std::isfinite(lod1) ||
		!std::isfinite(lod2)) {
		return kDefaultPixelThresholds;
	}

	lod0 = std::clamp(
		lod0,
		kMinimumPixelThreshold + kPixelThresholdGap * 2.0f,
		kMaximumPixelThreshold);
	lod1 = std::clamp(
		lod1,
		kMinimumPixelThreshold + kPixelThresholdGap,
		lod0 - kPixelThresholdGap);
	lod2 = std::clamp(
		lod2,
		kMinimumPixelThreshold,
		lod1 - kPixelThresholdGap);
	return { lod0, lod1, lod2 };
}

//============================================================================
//	GraphicsFeatureText namespaceMethods
//============================================================================
const char* Engine::GraphicsFeatureText::ToString(D3D_FEATURE_LEVEL value) {

	switch (value) {
	case D3D_FEATURE_LEVEL_12_2: return "12.2";
	case D3D_FEATURE_LEVEL_12_1: return "12.1";
	case D3D_FEATURE_LEVEL_12_0: return "12.0";
	case D3D_FEATURE_LEVEL_11_1: return "11.1";
	case D3D_FEATURE_LEVEL_11_0: return "11.0";
	default: return "Unknown";
	}
}

const char* Engine::GraphicsFeatureText::ToString(D3D_SHADER_MODEL value) {

	switch (value) {
#ifdef D3D_SHADER_MODEL_6_8
	case D3D_SHADER_MODEL_6_8: return "6.8";
#endif
#ifdef D3D_SHADER_MODEL_6_7
	case D3D_SHADER_MODEL_6_7: return "6.7";
#endif
	case D3D_SHADER_MODEL_6_6: return "6.6";
	case D3D_SHADER_MODEL_6_5: return "6.5";
	case D3D_SHADER_MODEL_6_4: return "6.4";
	case D3D_SHADER_MODEL_6_3: return "6.3";
	case D3D_SHADER_MODEL_6_2: return "6.2";
	case D3D_SHADER_MODEL_6_1: return "6.1";
	case D3D_SHADER_MODEL_6_0: return "6.0";
	case D3D_SHADER_MODEL_5_1: return "5.1";
	default: return "Unknown";
	}
}

const char* Engine::GraphicsFeatureText::ToString(D3D12_MESH_SHADER_TIER value) {

	switch (value) {
	case D3D12_MESH_SHADER_TIER_NOT_SUPPORTED: return "Not Supported";
	case D3D12_MESH_SHADER_TIER_1: return "Tier 1";
	default: return "Unknown";
	}
}

const char* Engine::GraphicsFeatureText::ToString(D3D12_RAYTRACING_TIER value) {

	switch (value) {
	case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: return "Not Supported";
	case D3D12_RAYTRACING_TIER_1_0: return "Tier 1.0";
	case D3D12_RAYTRACING_TIER_1_1: return "Tier 1.1";
	case D3D12_RAYTRACING_TIER_1_2: return "Tier 1.2";
	default: return "Unknown";
	}
}
