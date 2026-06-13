#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;
	class MultiRenderTarget;
	class DepthTexture2D;

	//============================================================================
	//	RenderPassExecutionHelper structures
	//============================================================================
	// 色サーフェスと外部DSVを個別に指定して描画する場合のバインド対象
	struct RenderPassSurfaceBinding {

		// 色を書き込むサーフェス
		MultiRenderTarget* colorSurface = nullptr;
		// 別サーフェスのDSVを使う場合に指定しnullptrならcolorSurfaceの深度を使う
		DepthTexture2D* depthOverride = nullptr;
	};

	namespace RenderPassExecutionHelper {

		// 描画パスの共通実行処理でリソース状態遷移とバインドとビューポート設定とディスパッチを行う
		// depthOverrideを渡すと色サーフェスとは別の深度をバインドできる、3Dテキストの深度遮蔽用
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			RenderPhase phase, MultiRenderTarget* target, MaterialPassKind passKind = MaterialPassKind::Draw,
			bool forceVertexMeshVariant = false, DepthTexture2D* depthOverride = nullptr);

		// 指定されたアイテムリストを使用して描画パスを実行する
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
			MultiRenderTarget* target, MaterialPassKind passKind = MaterialPassKind::Draw,
			bool forceVertexMeshVariant = false, bool depthOnly = false);

		// 色サーフェスと外部DSVを組み合わせて描画する背面法アウトライン用で
		// SceneFinalの色とSceneMainの深度を同時にバインドするために使う
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
			const RenderPassSurfaceBinding& surface, MaterialPassKind passKind = MaterialPassKind::Draw,
			bool forceVertexMeshVariant = false, bool depthOnly = false);

	} // RenderPassExecutionHelper
} // Engine
