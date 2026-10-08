#pragma once

//============================================================================
//	include
//============================================================================
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
	class ECSWorld;
	class GraphicsCore;
	class MultiRenderTarget;
	struct ResolvedCameraView;
	struct ResolvedRenderView;

	//============================================================================
	//	SceneComponentOverlayRenderer class
	//	SceneViewのライトとCameraアイコンを描画
	//============================================================================
	class SceneComponentOverlayRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneComponentOverlayRenderer();
		~SceneComponentOverlayRenderer();

		// アイコンを描画して選択候補を更新
		void Render(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			const ResolvedRenderView& view, MultiRenderTarget& surface,
			const ECSWorld* world, const SceneComponentOverlayItemList& items);
		// 描画資源を回収窓口へ渡す
		void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

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

		//--------- variables ----------------------------------------------------

		// アイコンの描画領域を渡す定数バッファ
		ViewConstantBuffer<SpriteViewConstants> spriteView_{ "SceneOverlaySpriteView" };
		// 同一Textureの連続描画ごとに保持する転送先
		std::vector<std::unique_ptr<StructuredInstanceBuffer<SpriteInstanceData>>> spriteRunInstances_{};
		// 転送前のアイコン情報
		std::vector<SpriteInstanceData> spriteScratch_{};

		// 出力形式ごとに保持するPSO
		std::unordered_map<uint64_t, PipelinePair> pipelineCache_{};

		// シェーダReflection名からRootBinding位置を引くためのキャッシュ
		PipelineBindingCache spriteBindingCache_{};
		PipelineBindingCache::SlotID spriteViewSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID spriteInstancesSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID spriteTextureSlot_ = PipelineBindingCache::kInvalidSlot;

		// 描画資源を初期化済みか
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

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
			PipelineState& pipeline, const SceneComponentOverlayItemList& items,
			SceneComponentOverlayItemList& renderedItems);
	};
}
