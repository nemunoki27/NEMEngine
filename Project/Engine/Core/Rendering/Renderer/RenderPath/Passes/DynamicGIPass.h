#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeaturePass.h"
#include "GIProbeSurfaceMask.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>

#include <array>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	DynamicGIPass class
	//	ワールドキャッシュと画面プローブを照明パスの前に更新する
	//============================================================================
	class DynamicGIPass final : public IRenderPass {
	public:
		explicit DynamicGIPass(const RenderPipelineDeps& deps);
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;
		void RetainViews(const std::unordered_set<std::string>& activeViews) override;
		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RenderFeature; }

	private:
		struct Emitter {
			Matrix4x4 world;
			Vector4 bounds;
			uint32_t instance;
			uint32_t geometry;
			uint32_t triangleCount;
			uint32_t flags;
		};
		using BrickCoord = std::array<int32_t, 3>;
		using BrickEntry = std::array<int32_t, 4>;
		struct BrickRegion {
			BrickCoord first, last, center;
			uint32_t sizeClass;
			bool operator==(const BrickRegion&) const = default;
		};
		struct AtmosphereColors {
			std::array<uint32_t, 2> lightIndices{ UINT32_MAX, UINT32_MAX };
			float groundHeight = 0.0f;
			float inverseHeight = 0.0f;
			std::array<std::array<Vector4, 32>, 2> colors{};
			float indirectIntensity = 1.0f;
			float emissiveContribution = 1.0f;
			uint32_t coarseShift = 2;
		};
		struct State {
			struct Geometry {
				GIProbeSurfaceMask::Bounds bounds;
				Matrix4x4 world{};
				uint32_t entityIndex = UINT32_MAX;
				uint32_t entityGeneration = 0;
				uint32_t renderFlags = 0;
			};
			std::vector<Geometry> geometry;
			std::vector<uint64_t> liveProbeMasks;
			GIProbeSurfaceMask surfaceMask;
			std::vector<GIProbeSurfaceMask::Bounds> changedBounds;
			uint64_t geometryGeneration = 0;
			uint64_t renderRevision = 0;
			uint64_t lightRevision = 0;
			uint64_t directionalRevision = 0;
			std::vector<std::pair<uint64_t, Vector4>> localLights;
			uint64_t environmentRevision = 0;
			uint64_t worldLightRevision = 0;
			uint64_t atmosphereRevision = 0;
			AtmosphereColors atmosphere;
			uint64_t materialGeneration = 0;
			uint32_t settlingFrames = 0;
			uint32_t lightingChangeFrames = 0;
			uint32_t changeFrames = 0;
			RenderFeatureProfileRuntime runtime{};
			DynamicGIQuality quality = DynamicGIQuality::Standard;
			const ECSWorld* world = nullptr;
			UUID sceneID{};
			std::vector<BrickRegion> brickRegions;
			std::vector<BrickEntry> bricks, lookup, previousLookup, uploadedBricks;
			DefaultStructuredInstanceBuffer<BrickEntry> brickBuffer{"gGIProbeBricks"};
			DefaultStructuredInstanceBuffer<BrickEntry> lookupBuffer{"gGIProbeLookup"};
			bool layoutPending = false;
			uint32_t syncHistoryFrames = 2;
			uint64_t lastCacheSerial = UINT64_MAX;
			StructuredRWBuffer<std::array<uint32_t, 2>> updateFrames{"gGIProbeUpdateFrames"};
			StructuredRWBuffer<Vector4> directLighting{"gGICachedLighting"};
			uint32_t directRevision = 1;
			uint32_t stableDirectFrames = 0;
			bool clearUpdateFrames = true;
			bool hasCoarseBricks = false;
			uint32_t cacheFrame = 0;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t mask = 0;
			uint32_t geometryLayers = 0;
			uint64_t generation = 0;
		};

		RenderFeaturePass executor_;
		StructuredRWBuffer<uint32_t> updateJobs_{"gGIUpdateJobs"};
		StructuredRWBuffer<uint32_t> updateArguments_{"gGIUpdateArguments"};
		ComPtr<ID3D12CommandSignature> dispatchSignature_;
		bool PrepareUpdateQueue(GraphicsCore& graphicsCore, SceneExecutionContext& context, uint32_t capacity);
		void BuildEmitterHierarchy();
		void MarkChangedProbes(const State& state);
		void UpdateAtmosphere(const SceneExecutionContext& context, State& state);
		const State* PrepareCacheChanges(GraphicsCore& graphicsCore, SceneExecutionContext& context, State& state);
		void UpdateProbeBricks(const SceneExecutionContext& context, State& state);
		FrameUploadBufferAllocator changeConstants_{4096};
		std::vector<Emitter> emitters_, emitterLeaves_, emitterTree_;
		std::vector<BrickRegion> brickRegions_;
		std::vector<BrickEntry> probeUpload_;
		std::vector<GIProbeSurfaceMask::Bounds> changedBounds_;
		uint64_t constantFrameSerial_ = UINT64_MAX;
		std::unordered_map<std::string, State> states_;
		uint64_t generation_ = 0;
		struct SharedCache {
			const State* state = nullptr;
			const void* tlas = nullptr;
			uint64_t frame = UINT64_MAX;
			MultiRenderTarget* color = nullptr;
			MultiRenderTarget* moments = nullptr;
		};
		SharedCache shared_{};
	};
}
