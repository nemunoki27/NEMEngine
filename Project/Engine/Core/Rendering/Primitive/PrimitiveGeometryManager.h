#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <cstdint>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class SRVDescriptor;
	struct PrimitiveRendererComponent;

	//============================================================================
	//	PrimitiveGeometry
	//	1形状分の共有GPUジオメトリ、頂点はMeshVertexで描画とレイトレとBLASで共用する
	//============================================================================
	struct PrimitiveGeometry {

		// 頂点はSRVで描画とレイトレの両方から読み、インデックスバッファは描画とBLASで使う
		MeshStructuredHandle<MeshVertex> vertexBuffer{};
		ImmutableIndexBuffer indexBuffer{};
		// レイトレのヒットシェーダーがインデックスを読むためのSRV
		MeshStructuredHandle<uint32_t> indexSRV{};
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;

		BottomLevelAccelerationStructure blas{};
		bool blasBuilt = false;

		// 最後に使われたフレーム、未使用が続いたら破棄する
		uint64_t lastUsedFrame = 0;
	};

	//============================================================================
	//	PrimitiveGeometryManager class
	//	形状とパラメータのハッシュ単位でジオメトリを生成し共有する
	//============================================================================
	class PrimitiveGeometryManager {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrimitiveGeometryManager() = default;
		~PrimitiveGeometryManager() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始、未使用ジオメトリを破棄する
		void BeginFrame();

		// ハッシュに対応するジオメトリを返す、無ければ生成する、使用フレームを更新する
		PrimitiveGeometry* GetOrCreate(GraphicsCore& graphicsCore, uint64_t hash,
			const PrimitiveRendererComponent& renderer);

		// 生成済みジオメトリを返す、無ければnullptr
		PrimitiveGeometry* Find(uint64_t hash);

		// レイトレ用にBLASを構築する、共有の頂点/インデックスから作る
		bool EnsureBLAS(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList, PrimitiveGeometry& geometry);

		// 全ジオメトリを破棄する
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 未使用ジオメトリを破棄するまでの猶予フレーム
		static constexpr uint64_t kEvictionFrames = 120;

		SRVDescriptor* srvDescriptor_ = nullptr;
		uint64_t frameIndex_ = 0;
		std::unordered_map<uint64_t, PrimitiveGeometry> geometries_{};

		//--------- functions ----------------------------------------------------

		// CPU生成しGPUバッファへアップロードする
		bool BuildGeometry(GraphicsCore& graphicsCore, const PrimitiveRendererComponent& renderer, PrimitiveGeometry& geometry);
	};
} // Engine
