#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayTypes.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

// c++
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	class AssetDatabase;
	class DepthTexture2D;
	class ECSWorld;
	class GraphicsCore;
	class MultiRenderTarget;
	struct ResolvedCameraView;
	struct ResolvedRenderView;

	//============================================================================
	//	SceneComponentOverlayRenderer class
	//	SceneView専用Overlayを通常RenderBatchを使わずに描く
	//============================================================================
	class SceneComponentOverlayRenderer {
	public:
		SceneComponentOverlayRenderer();
		~SceneComponentOverlayRenderer();

		void Render(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			const ResolvedRenderView& view, MultiRenderTarget& surface,
			DepthTexture2D* sceneDepth, ECSWorld* world,
			SceneComponentOverlayItemList& items);
		void Finalize();
	private:
		// Overlayアイコンをピクセル座標で描くためのViewサイズ
		struct SpriteViewConstants {

			Vector2 viewSize = Vector2::AnyInit(1.0f);
			Vector2 _pad = Vector2::AnyInit(0.0f);
		};
		static_assert(sizeof(SpriteViewConstants) % 16 == 0);

		// Overlayアイコン1枚分の矩形と色
		struct SpriteInstanceData {

			Vector2 center = Vector2::AnyInit(0.0f);
			Vector2 halfSize = Vector2::AnyInit(0.0f);
			Color4 color = Color4::White();
			float rotationRadians = 0.0f;
			Vector3 _pad = Vector3::AnyInit(0.0f);
		};
		static_assert(sizeof(SpriteInstanceData) % 16 == 0);

		// 出力RTV形式ごとに作るOverlay専用PSO
		struct PipelinePair {

			std::unique_ptr<PipelineState> sprite;
		};

		// 2Dアイコン用GPUバッファ
		ViewConstantBuffer<SpriteViewConstants> spriteView_{ "SceneOverlaySpriteView" };
		std::vector<std::unique_ptr<StructuredInstanceBuffer<SpriteInstanceData>>> spriteRunInstances_{};
		std::vector<SpriteInstanceData> spriteScratch_{};

		// 毎フレーム再生成しないためのPSO/テクスチャキーキャッシュ
		std::unordered_map<uint64_t, PipelinePair> pipelineCache_{};
		std::unordered_map<AssetID, std::string> textureKeyCache_{};

		// シェーダReflection名からRootBinding位置を引くためのキャッシュ
		PipelineBindingCache spriteBindingCache_{};
		PipelineBindingCache::SlotID spriteViewSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID spriteInstancesSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID spriteTextureSlot_ = PipelineBindingCache::kInvalidSlot;

		bool initialized_ = false;

		// GPUバッファを初期化する
		void Init(GraphicsCore& graphicsCore);
		// Surface形式に合うSprite用Pipelineを取得または生成する
		PipelinePair* GetOrCreatePipelines(GraphicsCore& graphicsCore, DXGI_FORMAT rtvFormat);
		// Builtin GUIDからテクスチャを要求し、ロード完了済みならGPUリソースを返す
		const GPUTextureResource* ResolveReadyTexture(GraphicsCore& graphicsCore,
			AssetDatabase& assetDatabase, AssetID textureAssetID);
		// 深度順の描画runごとに専用インスタンスバッファを用意する
		StructuredInstanceBuffer<SpriteInstanceData>& GetOrCreateSpriteRunBuffer(GraphicsCore& graphicsCore,
			size_t runIndex);
		// 深度なし・アルファありでSceneView用アイコンを2D描画する
		void DrawSpriteIcons(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			const ResolvedRenderView& view, MultiRenderTarget& surface,
			PipelineState& pipeline, SceneComponentOverlayItemList& items,
			SceneComponentOverlayItemList& renderedItems);
	};
}
