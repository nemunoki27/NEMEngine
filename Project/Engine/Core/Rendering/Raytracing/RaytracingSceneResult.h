#pragma once

//============================================================================
//	include
//============================================================================
#include "RaytracingStructures.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <array>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
	// Pickingで返すEntityとサブメッシュの対応
	struct MeshSubMeshPickRecord {

		Entity entity = Entity::Null();
		uint32_t subMeshIndex = 0;
		UUID subMeshStableID{};
	};

	//============================================================================
	//	RaytracingSceneResult class
	//	Scene構築結果とGPU転送先を所有する
	//============================================================================
	class RaytracingSceneResult {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 転送先とCPU配列を準備する
		void Init(GraphicsCore& graphicsCore);
		// 所有する資源と構築結果を解放する
		void Release();
		// 現在のFrameへ構築結果を転送する
		void Upload();
		// 現在のFrameに同じ内容があれば転送先を再利用する
		void UploadCached();
		// 構築結果を描画Contextへ公開する
		void Publish(SceneExecutionContext& context, ID3D12Resource* tlas,
			uint64_t materialGeneration, bool texturesReady, std::span<const RaytracingTLASInstance> instances);
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class RaytracingSceneBuilder;

		//--------- variables ----------------------------------------------------

		// シーンインスタンスバッファ
		DefaultStructuredInstanceBuffer<RaytracingInstanceShaderData> sceneInstances_{ "gRaytracingSceneInstances" };
		// BLAS内ジオメトリバッファ
		DefaultStructuredInstanceBuffer<RaytracingGeometryShaderData> sceneGeometries_{ "gRaytracingGeometries" };
		// サブメッシュインスタンスバッファ
		DefaultStructuredInstanceBuffer<MeshSubMeshShaderData> sceneSubMeshes_{ "gRaytracingSubMeshes" };

		DefaultStructuredInstanceBuffer<uint32_t> materialParameters_{ "gRaytracingMaterialParameters" };
		std::vector<uint32_t> materialParameterWords_;
		std::vector<RaytracingHitMaterial> hitMaterials_;

		// インスタンスデータ
		std::vector<RaytracingInstanceShaderData> sceneInstanceScratch_{};
		std::vector<RaytracingGeometryShaderData> sceneGeometryScratch_{};
		std::vector<RaytracingGeometryBounds> sceneGeometryBounds_{};
		std::vector<Matrix4x4> instanceWorldMatrices_{};
		bool instanceMatricesDirty_ = true;
		std::vector<MeshSubMeshShaderData> sceneSubMeshScratch_{};

		// メッシュピック用のサブメッシュ情報
		std::vector<MeshSubMeshPickRecord> scenePickRecords_{};
		// TLASインスタンスごとのピック記録先頭
		std::vector<uint32_t> scenePickRecordOffsets_{};

		uint32_t commonLayers_ = 0;
		bool probeReceiversCovered_ = false;
		std::array<uint64_t, kGraphicsFrameContextCount>
			sceneUploadFrameSerials_ = {};
	};
}
