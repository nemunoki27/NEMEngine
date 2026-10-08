#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <memory>
#include <span>
#include <vector>

namespace Engine {

	class ECSWorldLifetime;

	//============================================================================
	//	MeshBatchIdentityCache class
	//	Meshの構成とWorldの寿命を照合
	//============================================================================
	class MeshBatchIdentityCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 構築したバッチの識別情報を記録
		void Capture(const RenderSceneBatch& batch,
			std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh);
		// Worldの借用と識別情報を解除
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 構成と形状の更新世代が一致するか
		bool Matches(const RenderSceneBatch& batch,
			std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class MeshBatchResources;

		//--------- structure ----------------------------------------------------

		// バッチ構成とEntityの更新世代を記録
		struct CachedInstance {

			ECSWorld* world = nullptr;
			std::shared_ptr<const ECSWorldLifetime> worldLifetime;
			Entity entity{};
			AssetID material{};
			uint64_t batchKey = 0;
			uint64_t renderRevision = 0;
			uint64_t resetRevision = 0;
			uint64_t colorRevision = 0;
			uint32_t subMeshIndex = kAllMeshSubMeshes;
			uint32_t subMeshGroupIndex = UINT32_MAX;
			MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
			RenderPhase phase = RenderPhase::Opaque;
			BlendMode blend = BlendMode::Normal;
			bool receiveShadows = true;

			// 構成と更新世代を比較
			bool operator==(const CachedInstance&) const = default;
		};

		//--------- variables ----------------------------------------------------

		std::vector<CachedInstance> cachedInstances_;
		AssetID cachedMesh_{};
		uint32_t cachedMeshGeneration_ = 0;

		//--------- functions ----------------------------------------------------

		// 描画対象の識別情報を取得
		static CachedInstance MakeInstance(const RenderSceneBatch& batch, const RenderItem& item);
	};
}
