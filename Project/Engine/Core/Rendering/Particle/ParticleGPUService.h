#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUConstants.h"
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>

// c++
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class ECSWorld;
	class RenderAssetLibrary;
	class PipelineStateCache;
	class PipelineState;
	struct ParticleGPUInstance;
	struct ParticleGPUViewResources;

	struct ParticleGPUDrawData {

		ID3D12Resource* arguments = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS particles = 0;
		D3D12_GPU_VIRTUAL_ADDRESS parents = 0;
		D3D12_GPU_VIRTUAL_ADDRESS indices = 0;
		D3D12_GPU_VIRTUAL_ADDRESS counts = 0;
		ID3D12Resource* trailArguments = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS trails = 0;
		D3D12_GPU_VIRTUAL_ADDRESS trailPoints = 0;
		D3D12_GPU_VIRTUAL_ADDRESS trailSegments = 0;
		D3D12_GPU_VIRTUAL_ADDRESS ribbonOrder = 0;
	};

	//============================================================================
	//	ParticleGPUService class
	//	GPU粒子の実行資源と非同期読戻しを描画側で所有する
	//============================================================================
	class ParticleGPUService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleGPUService();
		~ParticleGPUService();

		// World更新の要求を各Viewより前に1回だけ実行する
		void UpdateWorld(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets, PipelineStateCache& pipelines);
		// Cameraごとのカリングと並べ替えを行う
		ParticleGPUDrawData PrepareView(GraphicsCore& graphics, const std::shared_ptr<ParticleGPUState>& state,
			RenderAssetLibrary& assets, PipelineStateCache& pipelines, const std::string& viewKey,
			ParticleGPUViewConstants constants);
		// GPUが決めた個数で共有Meshを描画する
		void DrawIndexed(GraphicsCore& graphics, const ParticleGPUDrawData& data, uint32_t phaseIndex);
		// 点列をCameraへ向けたリボンとして描画する
		void DrawTrails(GraphicsCore& graphics, const ParticleGPUDrawData& data);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<const ParticleGPUState*, std::unique_ptr<ParticleGPUInstance>> instances_;
		FrameConstantBufferAllocator constants_;
		ComPtr<ID3D12CommandSignature> indexedSignature_;
		ComPtr<ID3D12CommandSignature> trailSignature_;
		GraphicsResourceRetirement* retirement_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 完了したFenceだけを読み、古い再生世代を捨てる
		void CollectStatistics(GraphicsCore& graphics, ParticleGPUInstance& instance, ParticleGPUState& state);
		// 更新命令を記録する
		void Simulate(GraphicsCore& graphics, ParticleGPUInstance& instance, const PipelineState& pipeline,
			const PipelineState* trailPipeline, const PipelineState* ribbonPipeline, ParticleGPUState& state);
		// 粒子と独立したTrailの寿命と点列を更新する
		void UpdateTrails(GraphicsCore& graphics, ParticleGPUInstance& instance, const PipelineState& pipeline,
			const PipelineState* ribbonPipeline, const ParticleGPUSimulationStep& step, bool reset);
		// 発生順の新しい粒子を段階的に選別する
		void SelectRibbon(GraphicsCore& graphics, ParticleGPUInstance& instance,
			const PipelineState& pipeline, const ParticleGPUSimulationStep& step);
		// CameraごとのTrail描画要求を作る
		void PrepareTrailView(GraphicsCore& graphics, ParticleGPUInstance& instance, ParticleGPUViewResources& view,
			const PipelineState& pipeline, const PipelineState& sortPipeline, ParticleGPUViewConstants constants);
		// GPU結果を空いている読戻し領域へコピーする
		void ScheduleReadback(GraphicsCore& graphics, ParticleGPUInstance& instance, const ParticleGPUState& state);
		// 引数の型に合う間接描画Signatureを作る
		ID3D12CommandSignature* GetDrawSignature(GraphicsCore& graphics, bool indexed);
	};
}
