#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/Queue/RenderPassItemCollector.h>
#include <Engine/Core/Graphics/Render/View/ViewportRenderService.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Render/View/RenderFrameTypes.h>
#include <Engine/Core/Graphics/Render/Texture/RenderTargetRegistry.h>
#include <Engine/Core/Graphics/Render/Backend/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Graphics/Render/Backend/Registry/RenderExtractorRegistry.h>
#include <Engine/Core/Graphics/Render/Backend/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Graphics/Render/Pass/RenderItemBatchDispatcher.h>
#include <Engine/Core/Graphics/Render/Light/FrameLightBatch.h>
#include <Engine/Core/Graphics/Render/Light/Registry/LightExtractorRegistry.h>
#include <Engine/Core/Graphics/Render/Light/GPU/ViewLightBufferSet.h>
#include <Engine/Core/Graphics/Render/Light/GPU/ViewLightCullingBufferSet.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Material/MaterialResolver.h>
#include <Engine/Core/Graphics/Pipeline/PipelineStateCache.h>
#include <Engine/Core/Graphics/GPUBuffer/RenderBufferRegistry.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingSceneBuilder.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingViewBufferSet.h>
#include <Engine/Core/Graphics/GPUBuffer/DxConstBuffer.h>
#include <Engine/Core/Graphics/DxLib/ComPtr.h>
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>

// c++
#include <memory>

namespace Engine {

	// front
	struct SceneInstance;
	class DepthTexture2D;
	class SRVDescriptor;

	//============================================================================
	//	RenderPipelineRunner structures
	//============================================================================

	// レイトレーシング用のシーン実行時情報
	struct RaytracingSceneRuntimeContext {

		ID3D12Resource* tlasResource = nullptr;
		uint32_t instanceCount = 0;
	};
	struct HZBBuildConstants {

		// 読み込み元Mipのサイズ
		uint32_t sourceWidth = 1;
		uint32_t sourceHeight = 1;
		// 書き込み先Mipのサイズ
		uint32_t destWidth = 1;
		uint32_t destHeight = 1;
	};
	// HZBを参照するカリングシェーダ共通定数
	struct HZBCullingConstants {

		// HZBのMip0サイズ
		uint32_t width = 1;
		uint32_t height = 1;
		// カリング側で参照できるMip数
		uint32_t mipCount = 1;
		// HZB未生成やメニューOFF時は0にして安全側で描画する
		uint32_t occlusionEnabled = 0;
		// 深度の境界で誤って消えないようにするための余裕値
		float occlusionBias = 0.0005f;
		float _pad[3] = { 0.0f, 0.0f, 0.0f };
	};
	// GameViewの深度から階層Zを作成し、カリング用SRVとして登録するクラス
	class HierarchicalZBuffer {
	public:
		HierarchicalZBuffer() = default;
		~HierarchicalZBuffer() = default;

		// Viewサイズに合わせてHZBリソースとMipごとのDescriptorを用意する
		void EnsureSize(GraphicsCore& graphicsCore, uint32_t width, uint32_t height);
		// ZPrepass後のDepthTextureからMipチェーンを生成する
		void Build(GraphicsCore& graphicsCore, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, AssetDatabase& assetDatabase, DepthTexture2D* depthTexture);
		// RenderBufferRegistryへHZB本体とカリング定数を登録する
		void RegisterTo(RenderBufferRegistry& registry, bool occlusionEnabled);
		// 今フレームの深度でまだ生成されていない状態へ戻す
		void Invalidate();
		// DescriptorとGPUリソースを破棄する
		void Release(SRVDescriptor* srvDescriptor = nullptr);

		bool IsValid() const { return resource_ != nullptr; }
	private:
		// HZB本体。R32_FLOATのMip付きTextureとして保持する
		ComPtr<ID3D12Resource> resource_{};
		// Mip生成Compute用定数
		DxConstBuffer<HZBBuildConstants> buildConstants_{};
		// 描画カリング用定数
		DxConstBuffer<HZBCullingConstants> cullingConstants_{};
		// Mip単位のSRV/UAV。Computeで前Mipを読み、次Mipへ書く
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mipSRVHandles_{};
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mipUAVHandles_{};
		std::vector<uint32_t> mipSRVIndices_{};
		std::vector<uint32_t> mipUAVIndices_{};
		// Subresourceごとの状態を保持して余分なBarrierを避ける
		std::vector<D3D12_RESOURCE_STATES> mipStates_{};
		// 全Mip参照用SRV。描画カリング側はこちらを使う
		uint32_t fullSRVIndex_ = UINT32_MAX;
		D3D12_GPU_DESCRIPTOR_HANDLE fullSRVHandle_{};
		// 現在確保しているHZBのサイズ
		uint32_t width_ = 0;
		uint32_t height_ = 0;
		uint32_t mipCount_ = 0;
		SRVDescriptor* srvDescriptor_ = nullptr;
		// 今フレームのGameView深度からBuild済みか
		bool built_ = false;
		// 最後に登録したocclusion設定
		bool lastOcclusionEnabled_ = false;
		AssetID buildPipeline_{};

		// 現在のHZB状態をカリング用定数バッファへ反映する
		void UploadCullingConstants();
	};
	// シーンを処理する描画パスの実行に必要なコンテキスト
	struct SceneExecutionContext {

		// ビューの種類
		RenderViewKind kind = RenderViewKind::Game;
		// シーンインスタンスの情報
		const SceneInstance* sceneInstance = nullptr;
		const ResolvedRenderView* view = nullptr;
		const ResolvedRenderView* cullingView = nullptr;
		MultiRenderTarget* defaultSurface = nullptr;
		RenderTargetRegistry* targetRegistry = nullptr;
		// ツールプレビューなど、1枚のRT内の一部だけへ描く時の描画矩形
		bool useViewportRect = false;
		uint32_t viewportX = 0;
		uint32_t viewportY = 0;
		uint32_t viewportWidth = 0;
		uint32_t viewportHeight = 0;
		RenderBufferRegistry bufferRegistry{};
		// GameViewのDepthPrepass完了後にHZBを生成する対象
		HierarchicalZBuffer* hzbBuildTarget = nullptr;
		// 描画カリングで参照するHZB。SceneViewもGameViewのHZBを参照する
		HierarchicalZBuffer* hzbCullingSource = nullptr;
		// レイトレーシングの情報
		RaytracingSceneRuntimeContext raytracing{};
		// ツールプレビューなど、TLASを作らない描画ではRayQuery系Variantを選ばない。
		bool disableInlineRayTracing = false;
		// ツールプレビューではVertex版のGraphics Variantを優先する。
		bool forceVertexMeshVariant = false;
		// ECSワールドとシステムコンテキスト
		ECSWorld* world = nullptr;
		const SystemContext* systemContext = nullptr;
		AssetDatabase* assetDatabase = nullptr;
	};

	// エディタツール用のEntityプレビュー描画要求
	struct EntityPreviewRenderRequest {

		ECSWorld* world = nullptr;
		const SystemContext* systemContext = nullptr;
		AssetDatabase* assetDatabase = nullptr;

		const SceneHeader* sceneHeader = nullptr;
		UUID sceneInstanceID{};

		Entity rootEntity = Entity::Null();
		MultiRenderTarget* surface = nullptr;
		ManualRenderCameraState camera{};

		Color4 clearColor = Color4(0.08f, 0.10f, 0.14f, 1.0f);
		// falseなら既存のRT内容を保持したまま描画する。ProjectPanelのモデルプレビューAtlasで使用する。
		bool clearSurface = true;
		bool useViewportRect = false;
		uint32_t viewportX = 0;
		uint32_t viewportY = 0;
		uint32_t viewportWidth = 0;
		uint32_t viewportHeight = 0;
		// プレビュー用RenderTextureにだけ描画するグリッド
		bool drawGrid2D = false;
		bool drawGrid3D = false;
		// プレビューではMeshShader/RayQueryを避け、Vertex版の非RayQueryシェーダを優先する。
		bool forceVertexMeshVariant = true;
	};

	//============================================================================
	//	RenderPipelineRunner class
	//	描画パイプラインの実行を管理するクラス
	//============================================================================
	class RenderPipelineRunner {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderPipelineRunner() = default;
		~RenderPipelineRunner() = default;

		// 初期化
		void Init();

		// フレームの描画要求を受けて実行する
		void Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request);

		// 終了処理
		void Finalize();

		// 描画ビューのサーフェスをバックバッファに描画する
		bool PresentViewToBackBuffer(GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material = {});
		// エディタツール専用RenderTextureへ、指定Entityと子階層だけを描画する
		bool RenderEntityPreview(GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request);

		//--------- accessor -----------------------------------------------------

		ViewportRenderService& GetViewportRenderService() { return *viewportRenderService_.get(); }
		const ViewportRenderService& GetViewportRenderService() const { return *viewportRenderService_.get(); }

		// 種類に応じた描画ビューの情報の取得
		const ResolvedRenderView& GetResolvedView(RenderViewKind kind) const { return (kind == RenderViewKind::Game) ? gameView_ : sceneView_; }

		//今フレームの全ライト
		const FrameLightBatch& GetFrameLightBatch() const { return frameLightBatch_; }
		// ルートシーン用のビュー別ライト集合
		const PerViewLightSet& GetResolvedViewLightSet(RenderViewKind kind) const { return (kind == RenderViewKind::Game) ? gameViewLightSet_ : sceneViewLightSet_; }

		// ピック用のシーンビューのTLASリソースとサブメッシュ情報の取得
		ID3D12Resource* GetSceneViewTLASResource() const { return sceneViewTLASResource_; }
		const std::vector<MeshSubMeshPickRecord>& GetSceneViewPickRecords() const { return sceneViewPickRecords_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 描画バッチ
		RenderSceneBatch renderBatch_;
		// ライトバッチ
		FrameLightBatch frameLightBatch_{};
		// ビューポート描画サービス
		std::unique_ptr<ViewportRenderService> viewportRenderService_;
		// 描画ビュー情報
		ResolvedRenderView gameView_{};
		ResolvedRenderView sceneView_{};

		// レイトレーシングのパイプライン
		RaytracingPipelineStateCache raytracingPipelineStateCache_{};
		// ビュー関連のレイトレーシングバッファ
		RaytracingViewBufferSet gameViewRaytracingBuffers_{};
		RaytracingViewBufferSet sceneViewRaytracingBuffers_{};
		// GameView基準のカリング結果をSceneViewから確認するため、HZBはGameView用を共有する
		HierarchicalZBuffer gameViewHZB_{};
		// レイトレシーンの構築
		RaytracingSceneBuilder raytracingSceneBuilder_{};

		// シーンのTLASリソースとピック用のサブメッシュ情報
		ID3D12Resource* sceneViewTLASResource_ = nullptr;
		std::vector<MeshSubMeshPickRecord> sceneViewPickRecords_{};

		// 描画アイテム抽出器のレジストリ
		RenderExtractorRegistry extractorRegistry_{};
		// 描画アイテムの描画を行うレジストリ
		RenderBackendRegistry backendRegistry_{};
		// ライト抽出器のレジストリ
		LightExtractorRegistry lightExtractorRegistry_{};

		// 描画ビューごとの描画ターゲットレジストリ
		RenderTargetRegistry gameViewTargetRegistry_{};
		RenderTargetRegistry sceneViewTargetRegistry_{};

		// 描画アセット
		RenderAssetLibrary renderAssetLibrary_;
		PipelineStateCache pipelineStateCache_;
		MaterialResolver materialResolver_;

		// 描画アイテムのバッチングと描画の実行
		RenderItemBatchDispatcher batchDispatcher_{};

		// ルートシーン用のビュー別ライト集合
		PerViewLightSet gameViewLightSet_{};
		PerViewLightSet sceneViewLightSet_{};
		PerViewLightSet previewLightSet_{};
		// ビューごとのGPUライトバッファ
		ViewLightBufferSet gameViewLightBuffers_{};
		ViewLightBufferSet sceneViewLightBuffers_{};
		ViewLightCullingBufferSet gameViewLightCullingBuffers_{};
		ViewLightCullingBufferSet sceneViewLightCullingBuffers_{};
		// ツールプレビューは同一フレーム内に複数回描くため、ライトGPUバッファも描画ごとに分ける。
		FrameBatchResourcePool<ViewLightBufferSet> previewLightBufferPool_{};
		FrameBatchResourcePool<ViewLightCullingBufferSet> previewLightCullingBufferPool_{};

		// ツールプレビュー専用の描画バックエンド。メインビューのGPUバッファを上書きしないため分離する
		RenderBackendRegistry previewBackendRegistry_{};
		// 同一フレーム内の複数プレビューがGPUバッファを再利用して上書きしないための開始済みフラグ
		bool previewBackendFrameStarted_ = false;

		//--------- functions ----------------------------------------------------

		// 描画ビューのサーフェスを要求に応じて同期する
		void SyncRequestedSurfaces(GraphicsCore& graphicsCore, const RenderFrameRequest& request);
		// 描画ビューの情報を要求に応じて確定させる
		void ResolveViews(const RenderFrameRequest& request);

		// ビューの情報に応じたコンテキストを構築
		SceneExecutionContext BuildViewExecutionContext(GraphicsCore& graphicsCore,
			const RenderFrameRequest& request, const SceneInstance* sceneInstance,
			RenderViewKind kind, const ResolvedRenderView& view);
	};
} // Engine
