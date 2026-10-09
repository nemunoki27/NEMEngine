#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/World/ECS/Entity/WorldEntityKey.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

// c++
#include <memory>
#include <span>
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	struct SceneExecutionContext;
	struct SubMeshMaterial;
	struct SkinnedVertexSource;
	struct MeshRendererComponent;
	struct PrimitiveRendererComponent;
	struct PrimitiveGeometry;
	struct MaterialAsset;
	//============================================================================
	//	GlobalIlluminationGeometry class
	//	Camera別のGraph変形頂点をBLASへ渡す
	//============================================================================
	class GlobalIlluminationGeometry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// frameの定数領域を切り替える
		void BeginFrame();
		// 変形頂点とBLASを回収窓口へ渡す
		void Clear();
		// Skinning結果へGraphの頂点変形を適用
		bool PrepareMesh(GraphicsCore& graphicsCore, SceneExecutionContext& context, RenderAssetLibrary& library,
			ECSWorld* world, Entity entity, const MeshRendererComponent& renderer, const MeshGPUResource& mesh,
			std::span<const SubMeshMaterial> subMeshes, const Matrix4x4& worldMatrix, SkinnedVertexSource& source);
		// Primitiveの変形頂点からBLASを更新
		bool PreparePrimitive(GraphicsCore& graphicsCore, SceneExecutionContext& context, RenderAssetLibrary& library,
			ECSWorld* world, Entity entity, const PrimitiveRendererComponent& renderer, PrimitiveGeometry& geometry,
			const MaterialAsset& material, const MaterialParameterSet& parameters, const Matrix4x4& worldMatrix,
			const Matrix4x4& uvMatrix, SkinnedVertexSource& source, ID3D12Resource*& blas);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct GeometryEntry {

			StructuredRWBuffer<MeshVertex> vertices;
			BottomLevelAccelerationStructure blas;
			uint64_t lastFrame = 0;
			D3D12_GPU_VIRTUAL_ADDRESS geometryAddress = 0;
		};
		struct PipelineEntry {

			std::unique_ptr<PipelineState> pipeline;
			MaterialParameterLayout layout;
		};
		//--------- variables ----------------------------------------------------

		// WorldとEntity別の変形頂点
		std::unordered_map<WorldEntityKey, std::unique_ptr<GeometryEntry>, WorldEntityKeyHash> meshEntries_;
		std::unordered_map<WorldEntityKey, std::unique_ptr<GeometryEntry>, WorldEntityKeyHash> primitiveEntries_;
		// Graph別の頂点評価と共通の複製処理
		std::unordered_map<AssetID, PipelineEntry> pipelines_;
		std::unique_ptr<PipelineState> copyPipeline_;
		// GPU使用中の定数を上書きしない転送領域
		FrameConstantBufferAllocator allocator_;

		//--------- functions ----------------------------------------------------

		// Graphの頂点変形用Pipelineを取得
		PipelineEntry* FindPipeline(GraphicsCore& graphicsCore, RenderAssetLibrary& library, const MaterialAsset& material);
		// frame専用の定数領域へ転送
		D3D12_GPU_VIRTUAL_ADDRESS Upload(GraphicsCore& graphicsCore, const void* bytes, size_t size);
		// SubMeshの頂点変形をGPUへ投入
		bool Dispatch(GraphicsCore& graphicsCore, SceneExecutionContext& context, PipelineState& pipeline,
			GeometryEntry& entry, uint32_t descriptor, uint32_t offset, uint32_t count, uint32_t subMesh);
		// 変形前の頂点を作業領域へ複製
		bool CopyVertices(GraphicsCore& graphicsCore, SceneExecutionContext& context, GeometryEntry& entry,
			uint32_t descriptor, uint32_t offset, uint32_t count);
	};
}
