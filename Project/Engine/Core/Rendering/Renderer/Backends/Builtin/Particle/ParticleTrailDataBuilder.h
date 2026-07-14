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

	//============================================================================
	//	ParticleTrailDataBuilder namespace
	//	トレイル点列とセグメントごとのマテリアルデータを構築する
	//============================================================================
	namespace ParticleTrailDataBuilder {

		// 描画用のトレイル点列を構築する
		void Build(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			const ParticleCustomParameterLayout& customLayout, ParticleTrailRenderData& outData);
	}
} // Engine
