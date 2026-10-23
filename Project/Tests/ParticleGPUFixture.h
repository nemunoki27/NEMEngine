#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUResources.h>
#include <Engine/Core/Rendering/DxObject/Core/DxDevice.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandQueue.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>

namespace NEMTests {

	//============================================================================
	//	ParticleGPUFixture class
	//	実GPUで粒子の更新結果と描画要求を確認する
	//============================================================================
	class ParticleGPUFixture {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleGPUFixture();
		~ParticleGPUFixture();
		// Shaderを本番と同じReflectionとRoot設定で作る
		std::unique_ptr<Engine::PipelineState> MakePipeline(const char* filename);
		// 更新前の共有データを接続する
		void Upload(Engine::ParticleGPUInstance& instance, const Engine::ParticleGPUSimulationStep& step);
		// 粒子更新の1段を実行する
		void Simulate(Engine::ParticleGPUInstance& instance, const Engine::PipelineState& pipeline,
			const Engine::ParticleGPUSimulationStep& step, uint32_t operation);
		// 検証に必要な小さい範囲だけを読み戻す
		std::vector<uint8_t> Read(Engine::StructuredRWBuffer<uint32_t>& buffer, size_t bytes);
		// Copy元へ遷移済みの任意Bufferを読み戻す
		std::vector<uint8_t> ReadResource(ID3D12Resource* resource, size_t bytes);
		// GPU完了後に次の記録を始める
		void Finish();
		// 提出済み処理の完了後だけframe数を変更する
		void ChangeFrameCount(uint32_t count);
		// エラー以上のDevice診断がないことを確認する
		bool CheckMessages();

		Engine::DxDevice device;
		Engine::GraphicsResourceRetirement retirement;
		Engine::SRVDescriptor descriptors;
		Engine::DxCommandQueue queue;
		Engine::DxCommand commands;
		Engine::DxShaderCompiler compiler;
		Engine::FrameConstantBufferAllocator constants;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint32_t frameIndex_ = 0;
	};
}
