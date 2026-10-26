#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFeedbackTypes.h"
#include "ParticleGPUState.h"
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>

// c++
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class ECSWorld;
	class PipelineState;
	class RenderBufferRegistry;
	class RenderAssetLibrary;
	struct ParticlePaintReceiverComponent;

	// WorldごとにGPUイベントと実行時表面を所有する
	class ParticleGPUFeedbackWorld {
	public:

		// 全Systemの参照を確定して前回と今回のイベントを分ける
		void BeginUpdate(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets, const PipelineState& preparePipeline);
		// 前回のイベントを対象Systemの発生要求へ圧縮する
		void PrepareSource(GraphicsCore& graphics, ParticleGPUState& state, const PipelineState& preparePipeline);
		// 更新Shaderへ出力先と継承元を接続する
		void BindSource(GraphicsCore& graphics, const PipelineState& pipeline, ParticleGPUState& state, bool allowEvents = true);
		// 同じ描画frame内の後続更新へ発生要求を持ち越さない
		void ConsumeChildren(ParticleGPUState& state);
		// 全Systemの更新後に塗装と残留デカールを更新する
		void EndUpdate(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets,
			const PipelineState& paintPipeline, const PipelineState& decalPipeline, float deltaTime);
		// 停止中も新しい衝突を再適用せず消去と削除だけを反映する
		void RefreshPaint(GraphicsCore& graphics, ECSWorld& world, const PipelineState& paintPipeline);
		// 現在のViewへ共通のGPU表面を公開する
		void Publish(GraphicsCore& graphics, RenderBufferRegistry& registry);
		// Play終了とScene解放で実行時の所有を返す
		void Clear();
		// 生存粒子の継承値を残してSceneの表面と発生履歴を消す
		void ClearSurfaceHistory();

		const std::string& GetDiagnostic(const ParticleGPUState& state) const;
		const ParticleFeedbackStatisticsGPU& GetStatistics() const { return statistics_; }
		bool HasStatistics() const { return statisticsValid_; }
		bool HasRetainedState() const;
		// 発生先と継承値を持つSystemだけ拡張更新を使う
		bool RequiresSimulation(const ParticleGPUState& state) const;
	private:

		struct Source {

			std::weak_ptr<ParticleGPUState> owner;
			ParticleGPUIdentity identity{};
			std::vector<ParticleGPUEventRoute> routeData;
			StructuredInstanceBuffer<ParticleGPUEventRoute> routes;
			StructuredRWBuffer<ParticleGPUEvent> children;
			StructuredRWBuffer<uint32_t> childCounts;
			StructuredRWBuffer<ParticleGPUInheritedState> inheritedStates;
			uint32_t childCapacity = 0;
			uint32_t inheritedCapacity = 0;
			bool incoming = false;
			bool acceptsChildren = false;
			bool prepared = false;
			uint64_t preparedFrame = UINT64_MAX;
			bool active = false;
			bool childrenConsumed = false;
			bool resetInherited = false;
			std::string diagnostic;
		};
		struct PaintReceiver {

			std::unique_ptr<RenderTexture2D> texture;
			StructuredRWBuffer<uint32_t> tileCounts;
			StructuredRWBuffer<uint32_t> tileEvents;
			ParticlePaintReceiverGPU data{};
			uint32_t resolution = 0;
			uint32_t clearSerial = 0;
			bool reset = true;
			bool retained = false;
		};
		struct Readback {

			DxReadbackBuffer<ParticleFeedbackStatisticsGPU> buffer;
			GraphicsSubmissionTicket submission;
			uint64_t sequence = 0;
		};

		std::unordered_map<const ParticleGPUState*, std::unique_ptr<Source>> sources_;
		std::vector<ParticleGPUIdentity> identityData_;
		std::unordered_map<uint64_t, std::unique_ptr<PaintReceiver>> receivers_;
		std::unique_ptr<RenderTexture2D> emptyPaint_;
		StructuredRWBuffer<ParticleGPUEvent> events_[2];
		StructuredRWBuffer<uint32_t> eventCounts_[2];
		StructuredRWBuffer<uint32_t> counters_;
		StructuredRWBuffer<ParticleGPUEvent> emptyChildren_;
		StructuredRWBuffer<uint32_t> emptyChildCounts_;
		StructuredRWBuffer<ParticleGPUInheritedState> emptyInheritedStates_;
		StructuredInstanceBuffer<ParticleGPUIdentity> systems_;
		StructuredInstanceBuffer<ParticleGPUEventRoute> emptyRoutes_;
		StructuredInstanceBuffer<ParticleGPUEvent> emptyEvents_;
		StructuredInstanceBuffer<uint32_t> emptyCounts_;
		StructuredInstanceBuffer<ParticlePaintReceiverGPU> paintReceivers_;
		StructuredInstanceBuffer<DecalGPUData> decalStyles_;
		StructuredRWBuffer<ParticleImpactDecalGPU> decals_;
		StructuredRWBuffer<uint32_t> decalFreeList_;
		StructuredRWBuffer<uint32_t> decalBins_;
		StructuredRWBuffer<uint32_t> decalBinWords_;
		StructuredRWBuffer<std::array<uint32_t, 4>> decalArguments_;
		std::vector<DecalGPUData> styleData_;
		std::vector<std::array<TextureOwnership, 4>> styleTextures_;
		std::unordered_map<std::string, uint32_t> styleLookup_;
		std::array<Readback, kGraphicsFrameContextCount + 1> readbacks_;
		FrameConstantBufferAllocator constants_;
		ParticleFeedbackStatisticsGPU statistics_{};
		std::string diagnostic_;
		std::string statisticsDiagnostic_;
		uint64_t nextToken_ = 1;
		uint32_t writeIndex_ = 0;
		uint32_t eventCapacity_ = 0;
		uint32_t systemCount_ = 0;
		uint32_t receiverCount_ = 0;
		uint32_t decalCapacity_ = 0;
		uint32_t allocatedDecalCapacity_ = 0;
		uint64_t statisticsFence_ = 0;
		uint64_t surfaceSequence_ = 0;
		uint64_t statisticsSequence_ = 0;
		uint64_t decalEmissionSequence_ = 0;
		GraphicsSubmissionTicket decalEmissionSubmission_;
		bool decalEmissionPossible_ = false;
		bool initialized_ = false;
		bool eventsInitialized_ = false;
		uint64_t constantFrameSerial_ = UINT64_MAX;
		uint64_t defaultsFrameSerial_ = UINT64_MAX;
		bool statisticsValid_ = false;
		bool resetDecals_ = true;

		// Deviceと回収窓口へ最小限の領域を接続する
		void Initialize(GraphicsCore& graphics);
		// 停止中も現在のframeへ空入力と直前の識別子を公開する
		void PrepareDefaults(GraphicsCore& graphics);
		// 共通のイベント資源と定数を接続する
		void BindEvents(GraphicsCore& graphics, const PipelineState& pipeline, Source* source, uint32_t operation, bool allowEvents = true);
		// 衝突の出力先として塗装面を確定する
		void UpdatePaint(GraphicsCore& graphics, ECSWorld& world, const PipelineState& pipeline);
		// 元の粒子と独立した投影Poolを更新する
		void UpdateDecals(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets,
			const PipelineState& pipeline, float deltaTime);
		// 待機せずに集計だけを回収する
		void CollectStatistics(GraphicsCore& graphics);
		// 標準デカールMaterialの所有を上限付きで保持する
		uint32_t PrepareDecalStyle(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets,
			const ParticleImpactDecalSettings& settings);
	};
}
