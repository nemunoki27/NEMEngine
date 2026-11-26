#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeaturePass.h"
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
			uint32_t deformed;
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
		};
		struct State {
			struct Geometry {
				Vector4 bounds{};
				Matrix4x4 world{};
				uint32_t entityIndex = UINT32_MAX;
				uint32_t entityGeneration = 0;
				uint32_t renderFlags = 0;
			};
			std::vector<Geometry> geometry;
			std::array<Vector4, 8> changedBounds{};
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
			std::vector<BrickEntry> bricks, lookup, previousLookup;
			bool layoutPending = false;
			bool hasCoarseBricks = false;
			uint32_t cacheFrame = 0;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t mask = 0;
			uint32_t geometryLayers = 0;
			uint64_t generation = 0;
		};

		RenderFeaturePass executor_;
		void BuildEmitterHierarchy();
		void UpdateAtmosphere(const SceneExecutionContext& context, State& state);
		const State* PrepareCacheChanges(GraphicsCore& graphicsCore, SceneExecutionContext& context, State& state);
		void UpdateProbeBricks(const SceneExecutionContext& context, State& state);
		FrameUploadBufferAllocator changeConstants_{4096};
		std::vector<Emitter> emitters_, emitterLeaves_, emitterTree_;
		std::vector<BrickRegion> brickRegions_;
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
