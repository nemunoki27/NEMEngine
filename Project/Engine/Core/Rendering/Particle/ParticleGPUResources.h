#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUService.h"
#include "ParticleGPUTrailTypes.h"
#include "ParticleGPUWorldResources.h"
#include "Sources/ParticleGPUSourceResources.h"
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>

namespace Engine {

	struct ParticleGPUReadback {

		DxReadbackBuffer<ParticleStatisticsGPU> buffer;
		GraphicsSubmissionTicket submission;
		uint64_t epoch = 0;
		uint64_t sequence = 0;
	};

	struct ParticleGPUViewResources {

		struct Key { uint32_t value = 0; uint32_t particleIndex = 0; };
		StructuredRWBuffer<Key> keys[2];
		StructuredRWBuffer<uint32_t> counts;
		StructuredRWBuffer<uint32_t> histogram;
		StructuredRWBuffer<uint32_t> histogramOffsets;
		StructuredRWBuffer<uint32_t> arguments;
		StructuredRWBuffer<Key> trailKeys[2];
		StructuredRWBuffer<uint32_t> trailCounts;
		StructuredRWBuffer<ParticleUInt4GPU> trailArguments;
		uint64_t lastUsedFrame = 0;
		uint64_t preparedFrame = UINT64_MAX;
		uint64_t preparedEpoch = 0;
		uint64_t preparedSequence = 0;

		// Viewの領域を一組で用意する
		void Init(GraphicsCore& graphics, uint32_t capacity, uint32_t phaseCount, uint32_t trailSegmentCapacity);
	};

	struct ParticleGPUCheckpointBudget {

		static constexpr uint64_t kMemoryBudget = 128ull * 1024 * 1024;
		uint64_t allocated = 0;
		std::vector<std::pair<GraphicsSubmissionTicket, uint64_t>> retired;

		void Collect(uint64_t completedFence);
		void Release(uint64_t bytes, GraphicsSubmissionTicket submission);
	};

	struct ParticleGPUCheckpointResources {

		std::weak_ptr<ParticleGPUCheckpoint> owner;
		GraphicsResourceRetirement* retirement = nullptr;
		std::vector<ComPtr<ID3D12Resource>> buffers;
		Matrix4x4 emitterWorld = Matrix4x4::Identity();
		uint64_t memoryBytes = 0;
		std::shared_ptr<ParticleGPUCheckpointBudget> budget;
		GraphicsSubmissionTicket lastUse;

		~ParticleGPUCheckpointResources();
	};

	// 再生要求の所有がなくなったら回収窓口へ資源を渡す
	struct ParticleGPUInstance {

		std::weak_ptr<ParticleGPUState> owner;
		ParticleGPUWorldResources worldInputs;
		ParticleGPUSourceResources source;
		ParticleGPUDepthInput collisionDepth;
		StructuredRWBuffer<ParticleAttributesGPU> particles;
		StructuredRWBuffer<Vector4> moduleStates;
		StructuredRWBuffer<uint32_t> statistics;
		StructuredRWBuffer<ParticleTrailGPU> trails;
		StructuredRWBuffer<ParticleTrailPointGPU> trailPoints;
		StructuredRWBuffer<uint32_t> trailFreeList;
		StructuredRWBuffer<uint32_t> ribbonOrder;
		StructuredRWBuffer<ParticleUInt4GPU> ribbonCandidates[2];
		StructuredInstanceBuffer<ParticleGPUInstruction> instructions;
		StructuredInstanceBuffer<ParticleGPUCurveSample> curves;
		StructuredInstanceBuffer<ParticleGPUPhase> phases;
		StructuredInstanceBuffer<ParticleParentGPU> parents;
		std::array<ParticleGPUReadback, kGraphicsFrameContextCount + 1> readbacks;
		std::unordered_map<std::string, std::unique_ptr<ParticleGPUViewResources>> views;
		std::unordered_map<const ParticleGPUCheckpoint*, std::unique_ptr<ParticleGPUCheckpointResources>> checkpoints;
		std::shared_ptr<ParticleGPUCheckpointBudget> checkpointBudget;
		std::shared_ptr<const ParticleSystemDefinition> uploadedDefinition;
		std::array<std::shared_ptr<const ParticleSystemDefinition>, kGraphicsFrameContextCount> frameDefinitions;
		std::vector<ParticleParentGPU> lastParents;
		Matrix4x4 lastEmitterWorld = Matrix4x4::Identity();
		uint64_t epoch = 0;
		uint32_t capacity = 0;
		uint32_t phaseCount = 0;
		uint32_t trailCapacity = 0;
		uint32_t pointCapacity = 0;

		// GPUで書く領域とフレーム別Uploadを接続する
		void Init(GraphicsCore& graphics, const std::shared_ptr<ParticleGPUState>& state);
		// 描画先に依存しないDeviceと回収先で初期化する
		void Init(ID3D12Device* device, SRVDescriptor& descriptors, GraphicsResourceRetirement& retirement,
			const std::shared_ptr<ParticleGPUState>& state);
	};

	// 必要な入出力を名称から接続する
	void BindParticleGPUCompute(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline,
		D3D12_GPU_VIRTUAL_ADDRESS constants, ParticleGPUInstance& instance);
	// 粒子とTrailの共通ソート資源を接続する
	void BindParticleGPUViewCompute(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline,
		D3D12_GPU_VIRTUAL_ADDRESS constants, ParticleGPUInstance& instance, ParticleGPUViewResources& view,
		uint32_t input, uint32_t output, bool trail);
	// 同じCommandList内で途中状態の複製と復元を完了する
	void CaptureParticleGPUCheckpoint(GraphicsCore& graphics, ParticleGPUInstance& instance,
		const std::shared_ptr<ParticleGPUCheckpoint>& checkpoint);
	bool RestoreParticleGPUCheckpoint(GraphicsCore& graphics, ParticleGPUInstance& instance,
		const std::shared_ptr<const ParticleGPUCheckpoint>& checkpoint);
	void CaptureParticleGPUCheckpoint(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
		ParticleGPUInstance& instance, const std::shared_ptr<ParticleGPUCheckpoint>& checkpoint, uint64_t completedFence);
	bool RestoreParticleGPUCheckpoint(DxCommand& commands, ParticleGPUInstance& instance,
		const std::shared_ptr<const ParticleGPUCheckpoint>& checkpoint);
}
