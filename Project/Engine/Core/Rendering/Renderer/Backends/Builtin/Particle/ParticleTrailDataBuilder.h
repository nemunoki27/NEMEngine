#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleBatchResources.h>

// c++
#include <span>

namespace Engine {

	// front
	struct RenderDrawContext;
	struct RenderItem;
	struct TrailRendererRecord;
	struct BeamRendererRecord;

	//============================================================================
	//	ParticleTrailDataBuilder namespace
	//	トレイル点列とセグメントごとのマテリアルデータを構築する
	//============================================================================
	namespace ParticleTrailDataBuilder {

		void Build(const TrailRendererRecord& trail, BlendMode blendMode, const ParticleCustomParameterLayout& customLayout,
			ParticleTrailRenderData& outData);
		void Build(const BeamRendererRecord& beam, BlendMode blendMode, const Matrix4x4& uvMatrix, const ParticleCustomParameterLayout& customLayout,
			ParticleTrailRenderData& outData);

		// 描画用のトレイル点列を構築する
		void Build(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			const ParticleCustomParameterLayout& customLayout, ParticleTrailRenderData& outData);
	}
} // Engine
