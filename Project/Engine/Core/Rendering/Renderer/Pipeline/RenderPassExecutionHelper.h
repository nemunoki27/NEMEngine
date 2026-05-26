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

	namespace RenderPassExecutionHelper {

		// 描画パスの共通実行処理（リソース状態遷移、バインド、ビューポート設定、ディスパッチ）を行う。
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
			RenderPhase phase, MultiRenderTarget* target, const char* drawPassName = "Draw",
			bool forceVertexMeshVariant = false);

		// 指定されたアイテムリストを使用して描画パスを実行する。
		void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
			MultiRenderTarget* target, const char* drawPassName = "Draw",
			bool forceVertexMeshVariant = false, bool depthOnly = false);

	} // RenderPassExecutionHelper
} // Engine
