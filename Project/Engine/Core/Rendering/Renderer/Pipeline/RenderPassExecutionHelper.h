#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>

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
		// 別サーフェスのDSVを使う場合に指定する。nullptrならcolorSurfaceの深度を使う
		DepthTexture2D* depthOverride = nullptr;
	};

	namespace RenderPassExecutionHelper {

		// 描画パスの共通実行処理（リソース状態遷移、バインド、ビューポート設定、ディスパッチ）を行う
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			RenderPhase phase, MultiRenderTarget* target, const char* drawPassName = "Draw",
			bool forceVertexMeshVariant = false);

		// 指定されたアイテムリストを使用して描画パスを実行する
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
			MultiRenderTarget* target, const char* drawPassName = "Draw",
			bool forceVertexMeshVariant = false, bool depthOnly = false);

		// 色サーフェスと外部DSVを組み合わせて描画する。背面法アウトラインで
		// SceneFinalの色とSceneMainの深度を同時にバインドするために使う
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
			const RenderPassSurfaceBinding& surface, const char* drawPassName = "Draw",
			bool forceVertexMeshVariant = false, bool depthOnly = false);

	} // RenderPassExecutionHelper
} // Engine
