#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshSkinningSharedTypes.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;
	class SRVDescriptor;
	struct RenderDrawContext;
	struct MeshGPUResource;
	struct MeshRendererComponent;

	//============================================================================
	//	MeshBatchResources structures
	//============================================================================
	// 定数バッファ
	struct MeshViewConstants {

		// 実際に描画するビューの行列
		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
		// カリング判定に使うビューの行列でSceneViewではGameViewの行列になる
		Matrix4x4 cullingViewProjection = Matrix4x4::Identity();
		// Contribution CullingでカリングカメラのView空間へ変換する
		Matrix4x4 cullingView = Matrix4x4::Identity();
		// NormalCone判定で使用するカリングカメラ位置
		Vector3 cullingCameraPos = Vector3::AnyInit(0.0f);
		// Nearより手前に球がかかる場合はContribution判定を安全側で無効にする
		float cullingNearClip = 0.001f;
		// Hi-Z判定で球の最前面を求めるカリングカメラ前方
		Vector3 cullingCameraForward = Vector3(0.0f, 0.0f, 1.0f);
		float _cullingPad0 = 0.0f;
		// 描画先Viewportサイズ
		Vector2 viewSize = Vector2::AnyInit(1.0f);
		// カリング対象ViewportサイズでSceneView表示時もGameViewサイズを使う
		Vector2 cullingViewSize = Vector2::AnyInit(1.0f);
		// Projection行列のX/Y倍率でViewProjectionから取るとカメラ回転で値が崩れる
		Vector2 cullingProjectionScale = Vector2::AnyInit(1.0f);
		Vector2 _pad0 = Vector2::AnyInit(0.0f);
		// PBRライト計算に使う、実際に描画しているビューのカメラ位置
		Vector3 renderCameraPos = Vector3::AnyInit(0.0f);
		uint32_t frameSerial = 0;
	};
	static_assert(sizeof(MeshViewConstants) % 16 == 0);
	struct MeshIndirectArgsConstants {

		// ExecuteIndirectのDrawIndexedInstancedに渡すIndex数
		uint32_t indexCount = 0;
		uint32_t _pad[3] = { 0, 0, 0 };
	};
	// 頂点/メッシュシェーダインスタンスデータ
	struct MeshInstanceData {

		// エンティティワールド行列(位置・Bounds・Culling用)
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		// worldMatrixの法線変換行列transpose(inverse(worldMatrix))
		// 非一様スケール・負スケールでも法線が壊れないよう位置用とは別に持つ
		Matrix4x4 normalMatrix = Matrix4x4::Identity();

		// このインスタンスのサブメッシュ配列先頭
		uint32_t subMeshDataOffset = 0;
		uint32_t subMeshCount = 0;

		// スキニングするか
		uint32_t flags = 0;
		// スキニングする場合の、スキン頂点配列のオフセット
		uint32_t skinnedVertexOffset = 0;

		// このインスタンスが参照するアウトラインGPUデータのインデックス
		uint32_t outlineDataIndex = 0;
		// worldMatrixの線形部の行列式の符号で負スケールのmirror時に-1
		float orientationSign = 1.0f;
		// エディターラスターピック用のEntity ID
		uint32_t entityIndex = UINT32_MAX;
		uint32_t entityGeneration = UINT32_MAX;

		// per-instanceの乗算色tint、同マテリアルのまま個体ごとに色を変えるために使う
		Color4 color = Color4::White();
		uint32_t motionFrameSerial = 0;
		uint32_t _motionPad[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(MeshInstanceData) % 16 == 0);
	// MeshInstanceDataのflagsで、スキニングするか
	static constexpr uint32_t kMeshInstanceFlagSkinned = 1u;
	// MeshRenderFlagsから写すピクセル側で参照するフラグ
	static constexpr uint32_t kMeshInstanceFlagLighting = 1u << 1;
	static constexpr uint32_t kMeshInstanceFlagReceiveShadow = 1u << 2;
	static constexpr uint32_t kMeshInstanceFlagReceiveIBL = 1u << 3;
	static constexpr uint32_t kMeshInstanceFlagReceiveReflection = 1u << 4;

	// スキニングメッシュを持つエンティティの記録
	struct SkinnedEntityRecord {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();
		uint32_t vertexOffset = 0;
	};
	// メッシュインスタンスをEntityから検索するためのキー
	struct MeshEntityLookupKey {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();

		bool operator==(const MeshEntityLookupKey& rhs) const noexcept {
			return world == rhs.world && entity.index == rhs.entity.index &&
				entity.generation == rhs.entity.generation;
		}
	};
	struct MeshEntityLookupKeyHash {
		size_t operator()(const MeshEntityLookupKey& key) const noexcept {
			size_t h = std::hash<void*>{}(key.world);
			h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
			h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
			return h;
		}
	};

	//============================================================================
	//	MeshBatchResources class
	//	メッシュをインスタンシングで描画するためのリソースを管理するクラス
	//============================================================================
	class MeshBatchResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshBatchResources() = default;
		~MeshBatchResources();

		// 初期化
		void Init(GraphicsCore& graphicsCore);
		// 終了処理
		void Finalize();

		// 描画に使用するビューを更新する
		void UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView);
		void UploadBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh);
		// 静的バッチの構成を維持したままインスタンス行列だけを更新する
		bool RefreshInstanceTransforms(
			std::span<const RenderTransformChange> changes);
		// 静的キャッシュが保持するCPU配列を現在のフレーム用バッファへ転送する
		void UploadCachedBatchData();
		// 描画パスごとに変わるMeshDrawConstantsを毎描画更新しキャッシュヒット時も必ず呼ぶ
		void UpdateDrawConstants(const RenderDrawContext& drawContext, const MeshGPUResource& gpuMesh);
		// ExecuteIndirectで使用する頂点描画引数の定数を更新する
		void UpdateIndexedIndirectArgsConstants(uint32_t indexCount);

		// reflection駆動のサブメッシュ単位マテリアルパラメータを詰めて可変stride構造化バッファへ転送する
		void UploadSubMeshMaterialParams(const MaterialAsset* material,
			const MaterialParameterLayout& layout, const RenderDrawContext& drawContext);

		// スキニングに使用するリソースを確保する
		void EnsureSkinningResources(GraphicsCore& graphicsCore);

		//--------- accessor -----------------------------------------------------

		// スキニングするインスタンスの頂点オフセットを取得する
		bool FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const;

		// 現在のポーズに対するスキニング処理完了を記録する
		void MarkSkinningDispatched() {
			skinningDispatched_ = true;
			skinningOutputValid_ = true;
			dispatchedSkinningPoseHash_ = currentSkinningPoseHash_;
		}
		// スキニング頂点のリソース状態をセット
		void SetSkinnedVertexState(D3D12_RESOURCE_STATES state) { skinning_->skinnedVertexState = state; }
		// 圧縮頂点側のスキニング結果も通常頂点とは別に状態管理する
		void SetSkinnedPackedVertexState(D3D12_RESOURCE_STATES state) { skinning_->skinnedPackedVertexState = state; }

		// スキニング用のリソースがあるか
		bool HasSkinningResources() const { return skinning_ != nullptr; }

		// 内部リソースを取得する
		ID3D12Resource* GetSkinnedVerticesResource() const { return  skinning_->skinnedVertices.GetResource(); }
		ID3D12Resource* GetSkinnedPackedVerticesResource() const { return  skinning_->skinnedPackedVertices.GetResource(); }

		// GPUアドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress(RenderViewKind kind) const { return view_[ToViewIndex(kind)].GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstanceMeshGPUAddress() const { return meshData_.GetGPUAddress(); }
		// カリングComputeが書き込み、ExecuteIndirect/ASが読む可視インスタンス配列
		D3D12_GPU_VIRTUAL_ADDRESS GetVisibleInstanceMeshGPUAddress() const { return visibleMeshData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return drawGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndirectArgsConstantsGPUAddress() const { return indirectArgsGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshGPUAddress() const { return subMeshData_.GetGPUAddress(); }
		// reflection駆動のサブメッシュ単位マテリアルパラメータバッファ
		bool HasSubMeshMaterialParams() const { return subMeshParamAvailable_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshMaterialParamGPUAddress() const {
			return subMeshParamBuffer_.GetGPUAddress();
		}
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSubMeshMaterialParamGPUHandle() const {
			return subMeshParamHandles_[GraphicsFrameState::GetCurrentIndex()];
		}
		std::string_view GetSubMeshMaterialParamBindingName() const { return MaterialParameterCBuffer::kMesh; }
		// 背面法アウトラインのインスタンス別GPUデータ
		D3D12_GPU_VIRTUAL_ADDRESS GetOutlineGPUAddress() const { return outlineData_.GetGPUAddress(); }
		std::string_view GetOutlineBindingName() const { return outlineData_.GetBindingName(); }
		// ScreenSpaceOutline Mask描画用のper-draw定数(Style ID / SubMesh制限)
		D3D12_GPU_VIRTUAL_ADDRESS GetScreenSpaceOutlineMaskGPUAddress() const { return screenSpaceOutlineMaskGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningPaletteGPUAddress() const { return skinning_->skinningPalette.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningConstantsGPUAddress() const { return skinning_->skinningConstants.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedVerticesGPUAddress() const { return skinning_->skinnedVertices.GetGPUAddress(); }
		// MeshShader経路は圧縮頂点を読むため、スキニング後も圧縮頂点SRVを渡す
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedPackedVerticesGPUAddress() const { return skinning_->skinnedPackedVertices.GetGPUAddress(); }

		// SRV/UAVハンドルを取得する
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesSRVHandle() const { return skinning_->skinnedVertices.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesUAVHandle() const { return skinning_->skinnedVertices.GetUAVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedPackedVerticesSRVHandle() const { return skinning_->skinnedPackedVertices.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedPackedVerticesUAVHandle() const { return skinning_->skinnedPackedVertices.GetUAVGPUHandle(); }
		ID3D12Resource* GetIndexedIndirectArgsResource() const { return indexedIndirectArgs_.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndexedIndirectArgsGPUAddress() const { return indexedIndirectArgs_->GetGPUVirtualAddress(); }
		D3D12_RESOURCE_STATES GetIndexedIndirectArgsState() const { return indexedIndirectArgsState_; }
		void SetIndexedIndirectArgsState(D3D12_RESOURCE_STATES state) { indexedIndirectArgsState_ = state; }
		// カリング後に残ったインスタンスだけを格納するバッファ
		ID3D12Resource* GetVisibleInstanceMeshResource() const { return visibleMeshData_.GetResource(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetVisibleInstanceMeshSRVHandle() const { return visibleMeshData_.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetVisibleInstanceMeshUAVHandle() const { return visibleMeshData_.GetUAVGPUHandle(); }
		D3D12_RESOURCE_STATES GetVisibleInstanceMeshState() const { return visibleMeshDataState_; }
		void SetVisibleInstanceMeshState(D3D12_RESOURCE_STATES state) { visibleMeshDataState_ = state; }

		// 描画バウンディング名を取得する
		std::string_view GetViewBindingName() const { return "ViewConstants"; }
		std::string_view GetInstanceMeshBindingName() const { return meshData_.GetBindingName(); }
		std::string_view GetDrawBindingName() const { return "MeshDrawConstants"; }
		std::string_view GetIndirectArgsConstantsBindingName() const { return "IndirectArgsConstants"; }
		std::string_view GetSubMeshBindingName() const { return subMeshData_.GetBindingName(); }
		std::string_view GetSkinningPaletteBindingName() const { return "gSkinningPalette"; }
		std::string_view GetSkinningConstantsBindingName() const { return "SkinningConstants"; }
		std::string_view GetSkinnedVerticesBindingName() const { return "gSkinnedVertices"; }
		std::string_view GetSkinnedPackedVerticesBindingName() const { return "gSkinnedPackedVertices"; }

		// スキニング頂点バッファのSRVインデックスを取得する
		uint32_t GetSkinnedVerticesSRVIndex() const { return skinning_->skinnedVertices.GetSRVIndex(); }

		// インスタンス数を取得する
		uint32_t GetInstanceCount() const { return instanceCount_; }
		uint32_t GetSkinnedInstanceCount() const { return skinnedInstanceCount_; }
		uint64_t GetSkinningBufferGeneration() const { return skinningBufferGeneration_; }

		// スキニング処理をディスパッチしたか
		bool IsSkinningDispatched() const { return skinningDispatched_; }
		bool UsesFallbackTexture() const { return usesFallbackTexture_; }
		// スキニング頂点のリソース状態を取得する
		D3D12_RESOURCE_STATES GetSkinnedVertexState() const { return skinning_->skinnedVertexState; }
		D3D12_RESOURCE_STATES GetSkinnedPackedVertexState() const { return skinning_->skinnedPackedVertexState; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// スキニング用のリソースをまとめた構造体
		struct OptionalSkinningResources {

			StructuredInstanceBuffer<WellForGPU> skinningPalette{ "gSkinningPalette" };
			StructuredRWBuffer<MeshVertex> skinnedVertices{ "gSkinnedVertices" };
			// MeshShader用に法線をOct圧縮したスキニング結果を保持する
			StructuredRWBuffer<MeshPackedVertex> skinnedPackedVertices{ "gSkinnedPackedVertices" };
			ViewConstantBuffer<MeshSkinningDispatchConstants> skinningConstants{ "SkinningConstants" };

			D3D12_RESOURCE_STATES skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
			D3D12_RESOURCE_STATES skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
		};

		//--------- variables ----------------------------------------------------

		// ビューバッファ
		std::array<ViewConstantBuffer<MeshViewConstants>, 2> view_ = {
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" },
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" }
		};

		// バッファ
		DefaultStructuredInstanceBuffer<MeshInstanceData> meshData_{ "gMeshInstances" };
		// ExecuteIndirect/AmplificationShaderのカリング結果を書き戻す可視インスタンスバッファ
		StructuredRWBuffer<MeshInstanceData> visibleMeshData_{ "gVisibleMeshInstances" };
		// 同一フレーム内の複数パスで上書きしないper-draw定数領域
		PostProcessConstantBufferAllocator dynamicConstantAllocator_{};
		uint64_t dynamicConstantFrameSerial_ = 0;
		std::array<uint64_t, 2> viewUploadFrameSerials_ = { 0, 0 };
		std::array<Matrix4x4, 2> previousViewProjections_ = {
			Matrix4x4::Identity(), Matrix4x4::Identity()
		};
		std::array<bool, 2> previousViewValid_ = { false, false };
		D3D12_GPU_VIRTUAL_ADDRESS drawGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS screenSpaceOutlineMaskGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS indirectArgsGPUAddress_ = 0;
		DefaultStructuredInstanceBuffer<MeshSubMeshShaderData> subMeshData_{ "gSubMeshes" };
		// 背面法アウトラインのインスタンス別GPUデータ
		DefaultStructuredInstanceBuffer<MeshOutlineGPUData> outlineData_{ "gMeshOutlines" };

		// サブメッシュ単位マテリアルパラメータ用の可変stride構造化バッファ
		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		DxFrameMappedUploadBuffer subMeshParamBuffer_{};
		std::array<D3D12_GPU_DESCRIPTOR_HANDLE,
			kGraphicsFrameContextCount> subMeshParamHandles_{};
		std::array<uint32_t, kGraphicsFrameContextCount>
			subMeshParamSrvIndices_ = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
		std::vector<uint32_t> retiredSubMeshParamSrvIndices_{};
		uint32_t subMeshParamCapacityBytes_ = 0;
		uint32_t subMeshParamStride_ = 0;
		uint32_t subMeshParamElementCount_ = 0;
		bool subMeshParamAvailable_ = false;
		// UploadBatchDataで集めるインスタンス×サブメッシュ単位の上書きパラメータ
		std::vector<MaterialParameterSet> subMeshParamScratch_{};
		std::vector<uint8_t> subMeshParamPackedScratch_{};
		uint64_t subMeshParamLayoutHash_ = 0;
		const MaterialAsset* subMeshParamMaterial_ = nullptr;
		bool subMeshParamDirty_ = true;
		uint64_t subMeshParamDataGeneration_ = 1;
		std::array<uint64_t, kGraphicsFrameContextCount>
			uploadedSubMeshParamGenerations_ = { 0, 0, 0 };
		ComPtr<ID3D12Resource> indexedIndirectArgs_{};
		// ExecuteIndirect引数バッファの現在状態
		D3D12_RESOURCE_STATES indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;
		// 可視インスタンスバッファの現在状態
		D3D12_RESOURCE_STATES visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;

		// スキニング用バッファ
		std::unique_ptr<OptionalSkinningResources> skinning_{};

		// 毎バッチ再利用するデータ
		std::vector<MeshInstanceData> meshScratch_{};
		std::unordered_multimap<MeshEntityLookupKey, uint32_t,
			MeshEntityLookupKeyHash> meshInstanceIndexMap_{};
		std::vector<MeshSubMeshShaderData> subMeshScratch_{};
		std::vector<MeshOutlineGPUData> outlineScratch_{};

		// upload済みアウトラインデータから計算した保守的メトリクス
		struct OutlineBatchMetrics {

			float maxModelExpansion = 0.0f;
			float maxAbsCameraZOffset = 0.0f;
			bool hasScreenPixelWidth = false;
		};
		OutlineBatchMetrics outlineMetrics_{};

		// スキニング用の毎バッチ再利用するデータ
		std::vector<WellForGPU> paletteScratch_{};

		// スキニングメッシュを持つエンティティの記録
		std::vector<SkinnedEntityRecord> skinnedRecords_{};
		std::unordered_map<MeshEntityLookupKey, uint32_t,
			MeshEntityLookupKeyHash> skinnedVertexOffsetMap_{};

		// インスタンス数
		uint32_t instanceCount_ = 0;
		uint32_t skinnedInstanceCount_ = 0;

		// 初期化状態
		bool initialized_ = false;

		// スキニング処理をディスパッチしたか
		bool skinningDispatched_ = false;
		bool skinningOutputValid_ = false;
		uint64_t currentSkinningPoseHash_ = 0;
		uint64_t dispatchedSkinningPoseHash_ = 0;
		// 出力バッファ再生成を動的BLASへ伝える世代
		uint64_t skinningBufferGeneration_ = 0;
		bool usesFallbackTexture_ = false;

		//--------- functions ----------------------------------------------------

		// 現在のフレームスロットを再利用する前にper-draw定数の切り出し位置を戻す
		void BeginDynamicConstantsFrame();
		static constexpr size_t ToViewIndex(RenderViewKind kind) { return static_cast<size_t>(kind); }
	};
} // Engine

