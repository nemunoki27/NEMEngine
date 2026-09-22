#pragma once

//============================================================================
//	include
//============================================================================
#include "RaytracingStructures.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <array>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
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
		// 同じFrameへの重複転送を避ける
		void UploadCached();
		// 構築結果を描画Contextへ公開する
		void Publish(SceneExecutionContext& context, ID3D12Resource* tlas, uint64_t materialGeneration, bool texturesReady) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class RaytracingSceneBuilder;

		// シーンインスタンスバッファ
		StructuredInstanceBuffer<RaytracingInstanceShaderData> sceneInstances_{ "gRaytracingSceneInstances" };
		// BLAS内ジオメトリバッファ
		StructuredInstanceBuffer<RaytracingGeometryShaderData> sceneGeometries_{ "gRaytracingGeometries" };
		// サブメッシュインスタンスバッファ
		StructuredInstanceBuffer<MeshSubMeshShaderData> sceneSubMeshes_{ "gRaytracingSubMeshes" };

		// インスタンスデータ
		std::vector<RaytracingInstanceShaderData> sceneInstanceScratch_{};
		std::vector<RaytracingGeometryShaderData> sceneGeometryScratch_{};
		std::vector<MeshSubMeshShaderData> sceneSubMeshScratch_{};

		// メッシュピック用のサブメッシュ情報
		std::vector<MeshSubMeshPickRecord> scenePickRecords_{};
		// TLASインスタンスごとのピック記録先頭
		std::vector<uint32_t> scenePickRecordOffsets_{};

		std::array<uint64_t, kGraphicsFrameContextCount>
			sceneUploadFrameSerials_ = { 0, 0, 0 };
	};
}
