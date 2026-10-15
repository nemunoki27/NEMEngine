#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingTypes.h"
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>

// c++
#include <future>
#include <memory>
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class ECSWorldLifetime;
	struct SceneExecutionContext;

	//============================================================================
	//	BakedLightingRuntime class
	//	保存済み照明の読込とViewへの接続を管理する
	//============================================================================
	class BakedLightingRuntime {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BakedLightingRuntime() = default;
		~BakedLightingRuntime();

		// 同じSceneの完了済み結果を接続する
		void Update(GraphicsCore& graphics, SceneExecutionContext& context);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct Constants {

			uint32_t triangleCount = 0;
			uint32_t atlasSize = 0;
			uint32_t probeCount = 0;
			uint32_t captureCount = 0;
			Vector3 probeMinimum{};
			float probeSpacing = 1.0f;
			uint32_t probeDimensions[3]{};
			float surfaceTolerance = 0.05f;
			uint32_t receiverCount = 0;
			uint32_t padding[3]{};
		};
		struct Generation {

			std::future<BakedLightingData> loading;
			std::shared_ptr<BakedLightingData> data;
			std::weak_ptr<const uint8_t> databaseLifetime;
			uint64_t contentRevision = 0;
			std::vector<std::pair<AssetID, uint64_t>> assetRevisions;
			uint64_t lastUsed = 0;
			bool failed = false;
			uint32_t triangleCount = 0;
			uint32_t probeCount = 0;
			uint32_t captureCount = 0;
			MeshStructuredHandle<BakedLightingTriangle> triangles;
			MeshStructuredHandle<BakedLightingNode> nodes;
			MeshStructuredHandle<uint32_t> order;
			MeshStructuredHandle<Vector4> lightmaps;
			MeshStructuredHandle<BakedLightingProbe> probes;
			MeshStructuredHandle<BakedReflectionCapture> captures;
			MeshStructuredHandle<Vector4> reflectionPixels;
			struct WorldState {
				std::weak_ptr<const ECSWorldLifetime> lifetime;
				UUID sceneInstance{};
				uint64_t renderRevision = UINT64_MAX;
				uint64_t transformRevision = UINT64_MAX;
				uint64_t lightRevision = UINT64_MAX;
				bool current = false;
				std::vector<uint32_t> receiverEntities;
			};
			std::vector<WorldState> worlds;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<AssetID, std::unique_ptr<Generation>> generations_;
		std::vector<std::unique_ptr<Generation>> retired_;
		std::unique_ptr<Generation> empty_;
		ViewConstantBuffer<Constants> constants_{"BakedLightingConstants"};
		FrameUploadBufferAllocator receiverAllocator_{1024};

		//--------- functions ----------------------------------------------------

		// 完了したCPUデータを一組でGPUへ転送する
		void Upload(GraphicsCore& graphics, Generation& generation);
		// 固定背景と固定ライトの保存値を照合する
		bool IsCurrent(Generation& generation, const SceneExecutionContext& context);
		// 同じWorldとScene Instanceの検証状態を取得する
		Generation::WorldState& ResolveWorldState(Generation& generation, const SceneExecutionContext& context);
		// 描画レジストリへBufferを登録する
		void Register(GraphicsCore& graphics, Generation& generation, SceneExecutionContext& context, const Constants& constants);
	};
}
