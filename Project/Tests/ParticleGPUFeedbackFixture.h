#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Particle/ParticleGPUFeedbackTypes.h>

namespace NEMTests {

	// GPUイベントと本番粒子更新で同じ入力を使う
	struct ParticleGPUFeedbackFixture {

		ParticleGPUFixture& fixture;
		Engine::StructuredInstanceBuffer<Engine::ParticleGPUEventRoute> routes;
		Engine::StructuredInstanceBuffer<Engine::ParticleGPUIdentity> systems;
		Engine::StructuredInstanceBuffer<Engine::ParticleGPUEvent> previous;
		Engine::StructuredInstanceBuffer<uint32_t> previousCounts;
		Engine::StructuredRWBuffer<Engine::ParticleGPUEvent> events, children;
		Engine::StructuredRWBuffer<uint32_t> counts, childCounts, statistics;
		Engine::StructuredRWBuffer<Engine::ParticleGPUInheritedState> inherited;
		Engine::StructuredInstanceBuffer<Engine::ParticleGPUPhase> phases;
		Engine::StructuredInstanceBuffer<Engine::ParticleGPUInstruction> instructions;
		Engine::ParticleGPUEventConstants values;
		Engine::ParticleGPUEventRoute route;
		std::vector<Engine::ParticleGPUIdentity> identities{{Engine::Entity{1, 0}, 11, 1}, {Engine::Entity{2, 0}, 12, 1}};
		std::vector<Engine::ParticleGPUEvent> previousData{1};
		uint32_t previousCount = 0;
		bool writesColor = false;

		// 上限付きのイベントと継承領域を用意する
		explicit ParticleGPUFeedbackFixture(ParticleGPUFixture& target);
		// 最適化後のReflectionに残る入力だけを接続する
		void Bind(const Engine::PipelineState& pipeline, uint32_t operation, bool bindProgram = true);
		// 検証用の一段を実行する
		void Dispatch(const Engine::PipelineState& pipeline, uint32_t operation);
	};
}
