#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <array>
#include <unordered_map>

namespace Engine {

	class ECSWorld;
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

		struct BLASKey {

			AssetID meshAssetID{};
			// ホットリロード世代、差し替えで別キーになり古いBLASを再利用しない
			uint32_t reloadGeneration = 0;
			// 静的メッシュのLODごとにBLASを共有する
			uint32_t lodIndex = 0;
			// サブメッシュローカル行列を含むジオメトリ配置
			uint64_t geometryLayoutHash = 0;

			bool operator==(const BLASKey& rhs) const noexcept;
		};

		struct BLASKeyHash {
			size_t operator()(const BLASKey& key) const noexcept;
		};

		struct StaticInstanceBLASKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			uint32_t reloadGeneration = 0;

			bool operator==(const StaticInstanceBLASKey& rhs) const noexcept;
		};

		struct StaticInstanceBLASKeyHash {

			size_t operator()(const StaticInstanceBLASKey& key) const noexcept;
		};

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

		struct DynamicBLASKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			// ホットリロード世代、差し替えで別キーになり古いBLASを再利用しない
			uint32_t reloadGeneration = 0;

			bool operator==(const DynamicBLASKey& rhs) const noexcept;
		};

		struct DynamicBLASKeyHash {
			size_t operator()(const DynamicBLASKey& key) const noexcept;
		};

		struct DynamicBLASEntry {

			BottomLevelAccelerationStructure blas{};
			uint64_t poseGeneration = 0;
			uint64_t bufferGeneration = 0;
			uint64_t geometryLayoutHash = 0;
			D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
			uint64_t lastUsedFrame = 0;
			uint32_t consecutiveRefitCount = 0;
		};

		std::unordered_map<BLASKey, BottomLevelAccelerationStructure, BLASKeyHash> blases_;
		std::unordered_map<StaticInstanceBLASKey, StaticInstanceBLASEntry,
			StaticInstanceBLASKeyHash> staticInstanceBLASes_{};
		std::unordered_map<DynamicBLASKey,
			DynamicBLASEntry, DynamicBLASKeyHash> dynamicBlases_{};
		// メッシュごとに最後に構築したリロード世代、変化時に旧世代BLASを破棄する
		std::unordered_map<AssetID, uint32_t> meshBlasGeneration_;

	};
}
