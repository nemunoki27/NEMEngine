#include "MaterialRenderPolicy.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace {

	// AlphaClipを評価する標準PBRの深度Pass
	constexpr Engine::AssetID kMeshPBRDepth{0x4e454d4153534554ull, 0xa100000000000002ull};
}

uint64_t Engine::MaterialRenderPolicy::GetDefaultMaterialHash() {

	const auto& settings = DefaultMaterialSettings::GetInstance();
	uint64_t hash = std::hash<AssetID>{}(settings.GetMesh());
	Algorithm::HashCombine(hash, std::hash<AssetID>{}(settings.GetPrimitive()));
	return hash;
}

bool Engine::MaterialRenderPolicy::IsStandardSurface(const MaterialAsset& material) {

	if (material.shaderGraph) return false;
	bool hasSurface = false;
	for (const auto& pass : material.passes) {
		if (pass.passKind != MaterialPassKind::Draw && pass.passKind != MaterialPassKind::Masked &&
			pass.passKind != MaterialPassKind::ZPrepass && pass.passKind != MaterialPassKind::Transparent) continue;
		using namespace BuiltinAssets::Pipelines;
		if (pass.shaderOverride || (pass.pipeline != DefaultMesh && pass.pipeline != DefaultMeshMasked &&
			pass.pipeline != DefaultMeshTransparent && pass.pipeline != DefaultMeshZPrepass && pass.pipeline != kMeshPBRDepth &&
			pass.pipeline != DefaultPrimitive && pass.pipeline != DefaultPrimitiveTransparent)) return false;
		hasSurface = true;
	}
	return hasSurface;
}

bool Engine::MaterialRenderPolicy::IsOpaqueGeometry(const MaterialAsset* material, MaterialSurfaceMode mode) {

	// 未解決Materialや任意Shaderは候補Hitの判定を残す
	return material && mode == MaterialSurfaceMode::Opaque && IsStandardSurface(*material);
}
