#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleMotionSettings.h"
#include <Engine/Core/Assets/Formats/VectorFieldData.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <functional>
#include <memory>
#include <unordered_map>

namespace Engine {

	struct ParticleGPUForceField {

		Matrix4x4 worldToLocal = Matrix4x4::Identity();
		Vector4 parameters{0, 0, 0, 0};
		Vector4 axis{0, 0, 0, 0};
		uint32_t metadata[4]{};
	};
	static_assert(sizeof(ParticleGPUForceField) == 112);

	struct ParticleGPUVectorField {

		Matrix4x4 worldToGrid = Matrix4x4::Identity();
		Matrix4x4 vectorToWorld = Matrix4x4::Identity();
		uint32_t grid[4]{};
		Vector4 parameters{0, 0, 0, 0};
	};
	static_assert(sizeof(ParticleGPUVectorField) == 160);

	struct ParticleGPUSplineSample {

		Vector4 positionDistance{0, 0, 0, 0};
		Vector4 tangent{0, 0, 0, 0};
		Vector4 normal{0, 1, 0, 0};
	};
	static_assert(sizeof(ParticleGPUSplineSample) == 48);

	struct ParticleGPUSplineRange {

		uint32_t offset = 0;
		uint32_t count = 0;
		float length = 0;
		uint32_t closed = 0;
	};

	struct ParticleGPUCollider {

		// 角速度のXYZはsegmentStart.w、segmentEnd.w、velocity.wへ格納する
		Vector4 centerRadius{0, 0, 0, 0};
		Vector4 axisXExtent{0, 0, 0, 0};
		Vector4 axisYExtent{0, 0, 0, 0};
		Vector4 axisZExtent{0, 0, 0, 0};
		Vector4 segmentEnd{0, 0, 0, 0};
		Vector4 velocity{0, 0, 0, 0};
		// 形状とTriggerビット、Layer、Entityのindexとgeneration
		uint32_t metadata[4]{};
		Vector4 segmentStart{0, 0, 0, 0};
	};
	static_assert(sizeof(ParticleGPUCollider) == 128);

	struct ParticleGPUCollisionNode {

		Vector3 minimum{};
		uint32_t escape = 0;
		Vector3 maximum{};
		uint32_t collider = UINT32_MAX;
	};
	static_assert(sizeof(ParticleGPUCollisionNode) == 32);

	struct ParticleGPUHeightmap {

		Matrix4x4 worldToLocal = Matrix4x4::Identity();
		Matrix4x4 localToWorld = Matrix4x4::Identity();
		Vector4 size{0, 0, 0, 0};
		uint32_t grid[4]{};
		uint32_t receiver[4]{};
		Vector4 velocity{0, 0, 0, 0};
		Vector4 angularVelocity{0, 0, 0, 0};
	};
	static_assert(sizeof(ParticleGPUHeightmap) == 208);

	struct ParticleHeightmapData {

		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<float> samples;
	};

	// 深度の生成前に要求し、同じWorld更新の不透明深度だけを接続する
	struct ParticleGPUDepthRequest {

		Entity camera{};
		bool requested = false;
	};

	struct ParticleGPUWorldConstants {

		Matrix4x4 depthViewProjection = Matrix4x4::Identity();
		Matrix4x4 depthInverseViewProjection = Matrix4x4::Identity();
		Vector4 collision{0, 0, 0, 0};
		Vector4 depthParameters{0, 0, 0, 0};
		uint32_t counts[4]{};
		uint32_t options[4]{};
	};
	static_assert(sizeof(ParticleGPUWorldConstants) == 192);

	// 実行待ちStepとUpload先のframeスロットが共有する変更不可の入力
	struct ParticleGPUWorldSnapshot {

		std::vector<ParticleGPUForceField> forces;
		std::vector<ParticleGPUVectorField> vectorFields;
		std::vector<Vector4> vectorSamples;
		std::vector<ParticleGPUSplineSample> splineSamples;
		std::vector<ParticleGPUSplineRange> splineRanges;
		std::vector<ParticleGPUCollider> colliders;
		std::vector<ParticleGPUCollisionNode> collisionNodes;
		std::vector<ParticleGPUHeightmap> heightmaps;
		std::vector<float> heightSamples;
		ParticleGPUWorldConstants constants{};
		ParticleGPUDepthRequest depth{};
		std::string diagnostic;
	};

	struct ParticleGPUWorldLoaders {

		std::function<std::shared_ptr<const VectorFieldData>(AssetID)> vectorField;
		std::function<std::shared_ptr<const ParticleHeightmapData>(AssetID)> heightmap;
		std::function<std::string(AssetID)> diagnostic;
	};

	// 連続移動の境界からStack不要の衝突木を構築する
	void BuildParticleGPUCollisionBVH(ParticleGPUWorldSnapshot& snapshot, float deltaTime);

	class ParticleGPUWorldInputCache {
	public:

		// 親とスキニングの更新後にWorldごとに一度だけ呼ぶ
		void BeginFrame(const ECSWorld& world, float deltaTime, const ParticleGPUWorldLoaders& loaders);
		// 保存参照を所有Sceneで解決し、System固有の入力を固定する
		std::shared_ptr<const ParticleGPUWorldSnapshot> Build(const ECSWorld& world, Entity owner,
			std::span<const SceneEntityReference> splines, const ParticleCollisionSettings& collision, Entity mainGameCamera);
	private:

		struct SplineCache {

			uint64_t revision = 0;
			Matrix4x4 transform = Matrix4x4::Identity();
			std::vector<ParticleGPUSplineSample> samples;
			bool closed = false;
		};
		struct VectorAssetCache {

			std::weak_ptr<const VectorFieldData> owner;
			std::vector<Vector4> samples;
		};
		struct HeightAssetCache {

			std::weak_ptr<const ParticleHeightmapData> owner;
			bool valid = false;
		};

		ParticleGPUWorldSnapshot frame_;
		float deltaTime_ = 0;
		std::unordered_map<uint64_t, Vector3> previousCenters_;
		std::unordered_map<uint64_t, Quaternion> previousRotations_;
		std::unordered_map<uint64_t, Matrix4x4> previousHeightmapMatrices_;
		std::unordered_map<uint64_t, SplineCache> splines_;
		std::unordered_map<AssetID, VectorAssetCache> vectorAssets_;
		std::unordered_map<AssetID, HeightAssetCache> heightAssets_;
	};
}
