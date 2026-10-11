#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <array>
#include <memory>
#include <unordered_map>

namespace Engine {

	class ECSWorld;
	class ECSWorldLifetime;
	//============================================================================
	//	RaytracingBLASCache class
	//	形状ごとのBLASを保持する
	//============================================================================
	class RaytracingBLASCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 所有するBLASを解放する
		void Clear();
		// 使用されなくなったEntityのBLASを回収する
		void CollectExpired();
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class RaytracingSceneBuilder;

		//--------- structure ----------------------------------------------------

		// 共有Meshの形状世代とLODを区別するキー
		struct BLASKey {

			AssetID meshAssetID{};
			// 再読み込み後は別のBLASとして構築
			uint32_t reloadGeneration = 0;
			// 共有するLOD番号
			uint32_t lodIndex = 0;
			// サブメッシュローカル行列を含むジオメトリ配置
			uint64_t geometryLayoutHash = 0;

			// 形状世代とLODと配置が一致するか
			bool operator==(const BLASKey& rhs) const noexcept;
		};

		// 共有Meshの検索用Hash
		struct BLASKeyHash {
			// 検索用Hashを計算
			size_t operator()(const BLASKey& key) const noexcept;
		};

		// 固有変換を持つ静的Meshのキー
		struct StaticInstanceBLASKey {

			ECSWorld* world = nullptr;
			std::shared_ptr<const ECSWorldLifetime> worldLifetime;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			uint32_t reloadGeneration = 0;

			// 同じ形状とEntityの組み合わせか
			bool operator==(const StaticInstanceBLASKey& rhs) const noexcept;
		};

		// 静的Meshの検索用Hash
		struct StaticInstanceBLASKeyHash {

			// 検索用Hashを計算
			size_t operator()(const StaticInstanceBLASKey& key) const noexcept;
		};

		// 固有変換を反映したLOD別BLAS
		struct StaticInstanceBLASEntry {

			std::array<BottomLevelAccelerationStructure,
				kMeshLODCount> lodBLASes{};
			std::array<uint64_t, kMeshLODCount>
				lodGeometryLayoutHashes{};
			uint64_t geometryLayoutHash = 0;
			uint64_t lastUsedFrame = 0;
			bool layoutInitialized = false;
			bool dedicated = false;
		};

		// Skinning対象のEntityと形状世代
		struct DynamicBLASKey {

			ECSWorld* world = nullptr;
			std::shared_ptr<const ECSWorldLifetime> worldLifetime;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			// 再読み込み後は別のBLASとして構築
			uint32_t reloadGeneration = 0;

			// 同じ形状とEntityの組み合わせか
			bool operator==(const DynamicBLASKey& rhs) const noexcept;
		};

		// Skinning対象の検索用Hash
		struct DynamicBLASKeyHash {
			// 検索用Hashを計算
			size_t operator()(const DynamicBLASKey& key) const noexcept;
		};

		// Skinning用BLASと転送世代
		struct DynamicBLASEntry {

			BottomLevelAccelerationStructure blas{};
			uint64_t poseGeneration = 0;
			uint64_t bufferGeneration = 0;
			uint64_t geometryLayoutHash = 0;
			D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
			uint64_t lastUsedFrame = 0;
			uint32_t consecutiveRefitCount = 0;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<BLASKey, BottomLevelAccelerationStructure, BLASKeyHash> blases_;
		std::unordered_map<StaticInstanceBLASKey, StaticInstanceBLASEntry,
			StaticInstanceBLASKeyHash> staticInstanceBLASes_{};
		std::unordered_map<DynamicBLASKey,
			DynamicBLASEntry, DynamicBLASKeyHash> dynamicBlases_{};
		// 構築済みMeshの読み込み世代
		std::unordered_map<AssetID, uint32_t> meshBLASGeneration_;

	};
}
