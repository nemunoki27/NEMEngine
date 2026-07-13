#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderExtractorRegistry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Registry/LightExtractorRegistry.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/ViewLightBufferSet.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneBuilder.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingViewBufferSet.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <memory>
#include <vector>
#include <unordered_set>

namespace Engine {

	// front
	struct SceneInstance;
	class MeshRenderBackend;
	class PrimitiveRenderBackend;
	//============================================================================
	//	RenderPipelineRunner structures
	//============================================================================
	// レイトレーシング用のシーン実行時情報
	struct RaytracingSceneRuntimeContext {

		ID3D12Resource* tlasResource = nullptr;
		uint32_t instanceCount = 0;
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
		// 固定RenderPath用の中間レンダーターゲット
		RenderPathResources* resources = nullptr;
		// ビルボードの計算基準にするビュー
		const ResolvedRenderView* billboardView = nullptr;
		// ツールプレビューなど、1枚のRT内の一部だけへ描く時の描画矩形
		bool useViewportRect = false;
		uint32_t viewportX = 0;
		uint32_t viewportY = 0;
		uint32_t viewportWidth = 0;
		uint32_t viewportHeight = 0;
		RenderBufferRegistry bufferRegistry{};
		// レイトレーシングの情報
		RaytracingSceneRuntimeContext raytracing{};
		// ツールプレビューなど、TLASを作らない描画ではRayQuery系Variantを選ばない
		bool disableInlineRayTracing = false;
		// エディターピック用に、描画Raytracing設定とは独立してTLASだけを構築する
		bool requireRaytracingSceneForEditorPicking = false;
		// SceneViewのデフォルトグリッドを描画する
		bool drawSceneViewDefaultGrid = false;
		// 実エディターSceneViewの表示結果にだけSceneComponentOverlayを重ねる
		bool allowSceneComponentOverlay = false;
		// ツールプレビューではVertex版のGraphics Variantを優先する
		bool forceVertexMeshVariant = false;
		// ECSワールドとシステムコンテキスト
		ECSWorld* world = nullptr;
		const SystemContext* systemContext = nullptr;
		AssetDatabase* assetDatabase = nullptr;

		// ScreenSpaceOutline Mask描画用のper-draw値でScreenSpaceOutlineRendererが
		// Mask描画を呼ぶ直前に設定する、Mask以外のパスでは未使用
		uint32_t screenSpaceOutlineMaskStyleID = 0;
		int32_t screenSpaceOutlineMaskRestrictSubMeshIndex = -1;
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
		// falseなら既存のRT内容を保持したまま描画しProjectPanelのモデルプレビューAtlasで使用する
		bool clearSurface = true;
		bool useViewportRect = false;
		uint32_t viewportX = 0;
		uint32_t viewportY = 0;
		uint32_t viewportWidth = 0;
		uint32_t viewportHeight = 0;
		// プレビュー用RenderTextureにだけ描画するグリッド
		bool drawGrid2D = false;
		bool drawGrid3D = false;
		// プレビューではMeshShader/RayQueryを避け、Vertex版の非RayQueryシェーダを優先する
		bool forceVertexMeshVariant = true;
	};

	//============================================================================
	//	RenderPipelineRunner class
	//	描画パイプラインの実行を管理するクラス
	//============================================================================
	class RenderPipelineRunner {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenderPipelineRunner() = default;
		~RenderPipelineRunner() = default;

		// 初期化
		void Init();

		// フレームの描画要求を受けて実行する
		void Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request);

		// 終了処理
		void Finalize();

		// 外部編集されたメッシュをバックエンドで再ロードする、アセットのホットリロードから呼ぶ
		void ReloadMesh(AssetID meshAssetID);

		// 編集されたマテリアルのキャッシュを破棄して実行中に反映する、インスペクタ保存から呼ぶ
		void ReloadMaterial(AssetID materialAssetID);
		// 実行中にシェーダーとパイプラインを再構築する
		void ReloadShader(AssetID shaderAssetID);
		void ReloadPipeline(AssetID pipelineAssetID);

		// 構築済みグラフィックスパイプラインの統合reflectionを引く、未構築ならnullptr
		// マテリアルインスペクタがシェーダーの要求パラメータを自動列挙するために使う
		const ShaderReflectionInfo* FindPipelineGraphicsReflection(AssetID pipelineAssetID) const {
			return pipelineStateCache_.FindGraphicsReflection(pipelineAssetID);
		}

		// マテリアルのDrawパスのグラフィックスreflectionを引く、未構築やDrawパス無ならnullptr
		const ShaderReflectionInfo* FindMaterialDrawReflection(const MaterialAsset& material) const;

		// 描画ビューのサーフェスをバックバッファに描画する
		bool PresentViewToBackBuffer(GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material = {});
		// エディタツール専用RenderTextureへ、指定Entityと子階層だけを描画する
		bool RenderEntityPreview(GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request);

		//--------- accessor -----------------------------------------------------

		ViewportRenderService& GetViewportRenderService() { return *viewportRenderService_.get(); }
		const ViewportRenderService& GetViewportRenderService() const { return *viewportRenderService_.get(); }

		// 種類に応じた描画ビューの情報の取得
		const ResolvedRenderView& GetResolvedView(RenderViewKind kind) const { return (kind == RenderViewKind::Game) ? gameViewState_.view : sceneViewState_.view; }

		//今フレームの全ライト
		const FrameLightBatch& GetFrameLightBatch() const { return frameLightBatch_; }
		// ルートシーン用のビュー別ライト集合
		const PerViewLightSet& GetResolvedViewLightSet(RenderViewKind kind) const {
			return (kind == RenderViewKind::Game || gameViewState_.view.valid) ? gameViewState_.lightSet : sceneViewState_.lightSet;
		}

		// ピック用のTLASリソースとサブメッシュ情報の取得
		ID3D12Resource* GetGameViewTLASResource() const { return tlasResource_; }
		const std::vector<MeshSubMeshPickRecord>& GetGameViewPickRecords() const { return pickRecords_; }
		ID3D12Resource* GetSceneViewTLASResource() const { return tlasResource_; }
		const std::vector<MeshSubMeshPickRecord>& GetSceneViewPickRecords() const { return pickRecords_; }

		// 指定ビューのGBufferアタッチメントテクスチャを取得する、GBufferデバッグ表示用、未生成はnullptr
		RenderTexture2D* GetViewGBufferTexture(RenderViewKind kind, GBufferAttachment attachment);
		// 指定ビューのSceneMain深度テクスチャを取得する、GBufferデバッグ表示の深度用、未生成はnullptr
		DepthTexture2D* GetViewDepthTexture(RenderViewKind kind);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 描画ビュー1つ分の状態をまとめる、ゲーム/シーンの2ビューで同型を使う
		struct PerViewRenderState {

			ResolvedRenderView view{};
			RenderPathResources resources{};
			RaytracingViewBufferSet raytracingBuffers{};
			RenderTargetRegistry targetRegistry{};
			PerViewLightSet lightSet{};
			ViewLightBufferSet lightBuffers{};
		};

		//--------- variables ----------------------------------------------------

		// 描画バッチ
		RenderSceneBatch renderBatch_;
		// ライトバッチ
		FrameLightBatch frameLightBatch_{};
		// ビューポート描画サービス
		std::unique_ptr<ViewportRenderService> viewportRenderService_;
		// 描画ビューごとの状態、ゲーム/シーンで同型
		PerViewRenderState gameViewState_{};
		PerViewRenderState sceneViewState_{};

		// 固定RenderPath
		DeferredRenderPath renderPath_{};

		// レイトレーシングのパイプライン
		RaytracingPipelineStateCache raytracingPipelineStateCache_{};
		// レイトレシーンの構築でBillboardはゲームビューにのみ合わせるため1つでよい
		RaytracingSceneBuilder raytracingSceneBuilder_{};

		// ピック用のTLASリソースとサブメッシュ情報
		ID3D12Resource* tlasResource_ = nullptr;
		std::vector<MeshSubMeshPickRecord> pickRecords_{};

		// 描画アイテム抽出器のレジストリ
		RenderExtractorRegistry extractorRegistry_{};
		// 描画アイテムの描画を行うレジストリ
		RenderBackendRegistry backendRegistry_{};
		// ライト抽出器のレジストリ
		LightExtractorRegistry lightExtractorRegistry_{};

		// 描画アセット
		RenderAssetLibrary renderAssetLibrary_;
		PipelineStateCache pipelineStateCache_;
		MaterialResolver materialResolver_;

		// 描画アイテムのバッチングと描画の実行
		RenderItemBatchDispatcher batchDispatcher_{};
		// ComputeShader版PostProcess
		PostProcessExecutor postProcessExecutor_{};
		PostProcessTemporaryTargetPool postProcessTargetPool_{};
		PostProcessDebugInjector postProcessDebugInjector_{};
		PostProcessAssetGenerator postProcessAssetGenerator_{};

		// ツールプレビュー専用のライト集合
		PerViewLightSet previewLightSet_{};
		// ツールプレビューは同一フレーム内に複数回描くため、ライトGPUバッファも描画ごとに分ける
		FrameBatchResourcePool<ViewLightBufferSet> previewLightBufferPool_{};

		// ツールプレビュー専用の描画バックエンドでメインビューのGPUバッファを上書きしないため分離する
		RenderBackendRegistry previewBackendRegistry_{};
		// 同一フレーム内の複数プレビューがGPUバッファを再利用して上書きしないための開始済みフラグ
		bool previewBackendFrameStarted_ = false;

		// 前回通知したPostProcessStackアセットでシーン切り替え時の再ロードを検出するために使用
		AssetID lastNotifiedPostProcessStack_{};

		// ワールド切り替え時の静的バッチキャッシュ破棄用
		ECSWorld* lastRenderedWorld_ = nullptr;

		// 毎フレーム使い回すスクラッチで再確保を避ける
		std::unordered_set<AssetID> visibleMeshSet_{};
		std::vector<AssetID> visibleMeshes_{};
		RenderPassPhaseBuckets passBuckets_{};
		// 型付きMeshバックエンドのキャッシュで毎フレームのdynamic_castを避ける
		MeshRenderBackend* meshBackend_ = nullptr;
		MeshRenderBackend* previewMeshBackend_ = nullptr;
		PrimitiveRenderBackend* primitiveBackend_ = nullptr;

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

