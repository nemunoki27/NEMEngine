#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleBatchResources.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>

namespace Engine {

	struct Particle;
	struct ParticleShapeData;
}

namespace Engine::ParticleDrawPreparation {

	// UVの変換行列を生成する
	Engine::Matrix4x4 BuildParticleUVMatrix(const Engine::Particle& particle);

	// 静的な形状データを作成する
	Engine::ParticleShapeData MakeStaticShapeData(const Engine::ParticleRenderSettings& settings);

	// PhaseのMaterial設定を取得する
	const Engine::ParticlePhaseMaterialSettings& GetPhaseMaterialSettings(
		const Engine::ParticleRenderSettings& settings, size_t phaseIndex);

	// Phaseの定数値をMaterialへ反映する
	void BuildPhaseMaterialInstance(
		const Engine::ParticlePhaseMaterialSettings& materialSettings,
		Engine::MaterialParameterSet& outInstance);

	// 描画空間と互換性からMaterialを選択する
	bool ResolveParticlePass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterial,
		bool is2D, Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved);

	// Phaseごとの描画データを収集する
	void CollectParticleInstances(const RenderDrawContext& context,
		std::span<const RenderItem* const> items, const std::vector<ParticleCustomParameterLayout>& customLayouts,
		std::vector<ParticleDrawInstanceData>& outInstances, std::vector<uint32_t>& outPhaseCounts,
		std::vector<uint8_t>& outCustomParameters, std::vector<uint32_t>& outCustomOffsets);
}
