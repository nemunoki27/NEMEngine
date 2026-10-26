#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"
#include <Engine/Core/Rendering/Particle/ParticleGPUProgram.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleCustomDataSettings.h>

// c++
#include <memory>

namespace Engine {

	struct ParticleRenderSettings;
	struct ParticleSystemSettings;

	struct ParticleGPUMaterialCurveHeader {

		uint32_t offset = 0;
		uint32_t count = 0;
		uint32_t reserved[2]{};
	};
	static_assert(sizeof(ParticleGPUMaterialCurveHeader) == 16);

	struct ParticleGPUMaterialCurves {

		std::vector<ParticleGPUMaterialCurveHeader> headers;
		std::vector<ParticleGPUCurveSample> samples;
	};

	// 任意数値とMaterialの曲線を共有テーブルへ変換する
	ParticleGPUMaterialCurves BuildParticleGPUMaterialCurves(const ParticleCustomParameterLayout& layout,
		const ParticlePhaseMaterialSettings& material, const ParticleCustomDataSettings& customData);

	// 編集設定とShaderの世代が変わるまで共有曲線を再利用する
	class ParticleGPUMaterialCurveCache {
	public:
		const ParticleGPUMaterialCurves& Get(const ParticleRenderSettings& settings, AssetID materialID,
			uint32_t phase, uint64_t shaderRevision, const ParticleCustomParameterLayout& layout,
			const ParticlePhaseMaterialSettings& material, const ParticleCustomDataSettings& customData);
		void BeginFrame();
	private:
		struct Entry {

			std::weak_ptr<const ParticleSystemSettings> owner;
			AssetID materialID{};
			uint32_t phase = 0;
			uint64_t shaderRevision = 0;
			ParticleGPUMaterialCurves curves;
		};
		std::vector<Entry> entries_;
		ParticleGPUMaterialCurves transient_;
	};
}
