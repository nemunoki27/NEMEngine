#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/Graphics/GPUBuffer/DxStructuredBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/IndexBuffer.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Core/Graphics/Mesh/MeshNode.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Vector4.h>
#include <Engine/MathLib/Color.h>
#include <Engine/MathLib/Quaternion.h>

namespace Engine {

	//============================================================================
	//	MeshResourceTypes structures
	//============================================================================

	// 頂点データ
	struct MeshVertex {

		Vector3 normal = Vector3::AnyInit(0.0f);
		Vector2 uv = Vector2::AnyInit(0.0f);

		Vector4 position = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	};

	// キーフレーム
	template <typename TValue>
	struct Keyframe {

		float time = 0.0f;
		TValue value{};
	};
	using KeyframeVector3 = Keyframe<Vector3>;
	using KeyframeQuaternion = Keyframe<Quaternion>;

	// アニメーションカーブ
	template <typename TValue>
	struct AnimationCurve {

		std::vector<Keyframe<TValue>> keyframes{};
	};

	// 骨のノードアニメーション
	struct NodeAnimation {

		AnimationCurve<Vector3> scale{};
		AnimationCurve<Quaternion> rotate{};
		AnimationCurve<Vector3> translate{};
	};

	// アニメーションデータ
	struct AnimationData {

		float duration = 0.0f;
		std::unordered_map<std::string, NodeAnimation> nodeAnimations{};
	};

	// ジョイント(骨)データ
	struct Joint {

		NodeTransform transform{};
		bool isParentTransform = false;

		Matrix4x4 localMatrix = Matrix4x4::Identity();
		Matrix4x4 skeletonSpaceMatrix = Matrix4x4::Identity();

		std::string name{};
		std::vector<int32_t> children{};
		int32_t index = -1;
		std::optional<int32_t> parent{};
	};

	// スケルトンデータ
	struct Skeleton {

		int32_t root = -1;
		std::unordered_map<std::string, int32_t> jointMap{};
		std::vector<Joint> joints{};
		std::string name{};
	};

	// 1頂点あたりの最大影響ジョイント数
	static constexpr uint32_t kNumMaxInfluence = 4;
	struct VertexInfluence {

		std::array<float, kNumMaxInfluence> weights = { 0.0f,0.0f,0.0f,0.0f };
		std::array<int32_t, kNumMaxInfluence> jointIndices = { -1,-1,-1,-1 };
	};
	// GPU上でのウェルデータ
	struct WellForGPU {

		Matrix4x4 skeletonSpaceMatrix = Matrix4x4::Identity();
		Matrix4x4 skeletonSpaceInverseTransposeMatrix = Matrix4x4::Identity();
	};
	// スキンクラスター
	struct SkinCluster {

		std::vector<Matrix4x4> inverseBindPoseMatrices{};
	};

	// メッシュのテクスチャセット
	struct ImportedMeshTextureSet {

		std::string baseColorTexturePath;
		std::string normalTexturePath;
		std::string metallicRoughnessTexturePath;
		std::string specularTexturePath;
		std::string emissiveTexturePath;
		std::string occlusionTexturePath;
	};
	// path -> AssetID解決後の結果
	struct ImportedMeshTextureAssetSet {

		AssetID baseColorTexture{};
		AssetID normalTexture{};
		AssetID metallicRoughnessTexture{};
		AssetID specularTexture{};
		AssetID emissiveTexture{};
		AssetID occlusionTexture{};
	};
	// メッシュレット情報
	struct MeshletDesc {

		uint32_t vertexOffset = 0;
		uint32_t vertexCount = 0;

		uint32_t primitiveOffset = 0;
		uint32_t primitiveCount = 0;

		uint32_t subMeshIndex = 0;
		uint32_t _pad[3] = { 0, 0, 0 };
	};

	// サブメッシュの情報
	struct SubMeshDesc {

		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;

		// subMesh -> meshlet範囲
		uint32_t meshletOffset = 0;
		uint32_t meshletCount = 0;

		// 表示/識別用
		std::string name;
		std::string materialName;

		// デフォルトのテクスチャセット
		ImportedMeshTextureSet defaultTextures{};
		ImportedMeshTextureAssetSet defaultTextureAssets{};
		// デフォルトのベースカラー
		Color4 baseColor = Color4::White();
	};
	// 読みこまれたメッシュアセットの情報
	struct ImportedMeshAsset {

		// アセットID
		AssetID assetID{};
		// ソースファイルのパス
		std::string sourcePath;

		// 頂点データ
		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMeshDesc> subMeshes;

		// 頂点がどのサブメッシュに属するかの情報
		std::vector<uint32_t> vertexSubMeshIndices;

		// メッシュレット化済みデータ
		std::vector<MeshletDesc> meshlets;
		std::vector<uint32_t> meshletVertexIndices;
		std::vector<uint32_t> meshletPrimitiveIndices;

		MeshNode rootNode{};

		// スキニング情報
		bool isSkinned = false;
		uint32_t boneCount = 0;
		std::vector<VertexInfluence> vertexInfluences{};
	};
	// インスタンシングリソース
	template <typename T>
	struct MeshStructuredHandle {

		std::unique_ptr<DxStructuredBuffer<T>> buffer;
		uint32_t srvIndex = UINT32_MAX;
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle{};

		// リソース解放
		void Release(SRVDescriptor* srvDescriptor) {

			if (srvDescriptor && srvIndex != UINT32_MAX) {
				srvDescriptor->Free(srvIndex);
			}
			srvIndex = UINT32_MAX;
			srvGPUHandle = {};
			buffer.reset();
		}
	};
	// GPU上のメッシュリソース
	struct MeshGPUResource {

		// アセットID
		AssetID assetID{};

		// バッファ
		MeshStructuredHandle<MeshVertex> vertexSRV;
		MeshStructuredHandle<uint32_t> indexSRV;
		MeshStructuredHandle<uint32_t> vertexSubMeshIndexSRV;

		// PrimitiveIndex()->サブメッシュインデックス参照用
		MeshStructuredHandle<uint32_t> primitiveSubMeshIndexSRV;

		// メッシュレット
		MeshStructuredHandle<MeshletDesc> meshletSRV;
		MeshStructuredHandle<uint32_t> meshletVertexIndexSRV;
		MeshStructuredHandle<uint32_t> meshletPrimitiveIndexSRV;

		// スキニング
		MeshStructuredHandle<VertexInfluence> skinInfluenceSRV;

		IndexBuffer indexBuffer;

		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;
		uint32_t meshletCount = 0;
		std::vector<SubMeshDesc> subMeshes;

		// スキニングするか
		bool isSkinned = false;
		// 骨の数
		uint32_t boneCount = 0;
	};
} // Engine