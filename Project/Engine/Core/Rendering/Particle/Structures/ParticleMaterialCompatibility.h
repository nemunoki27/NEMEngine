#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	ParticleMaterialCompatibility structures
	//	Particle描画へ適用できるマテリアル契約の検証結果
	//============================================================================
	enum class ParticleMaterialCompatibilityStatus :
		uint8_t {

		Compatible,
		PendingReflection,
		Incompatible,
	};

	struct ParticleMaterialCompatibilityResult {

		ParticleMaterialCompatibilityStatus status = ParticleMaterialCompatibilityStatus::Incompatible;
		std::string message{};

		bool IsCompatible() const { return status == ParticleMaterialCompatibilityStatus::Compatible; }
	};

	// Material用途とシェーダーreflectionからParticle描画との互換性を検証する
	ParticleMaterialCompatibilityResult CheckParticleMaterialCompatibility(
		const MaterialAsset& material, const ShaderReflectionInfo* reflection);
} // Engine
