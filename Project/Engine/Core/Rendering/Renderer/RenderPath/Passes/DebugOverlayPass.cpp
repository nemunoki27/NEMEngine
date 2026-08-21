#include "DebugOverlayPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// c++
#include <array>

//============================================================================
//	DebugOverlayPass classMethods
//============================================================================

void Engine::DebugOverlayPass::Execute([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, [[maybe_unused]] SceneExecutionContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// SceneViewのときだけグリッドやデバッグ線を最後に重ねる
	if (context.kind == RenderViewKind::Scene && context.defaultSurface && context.view) {

		if (context.drawSceneView2DCameraBounds && context.gameView &&
			context.gameView->orthographic.valid) {

			// GameViewのNDC四隅をワールドへ戻し、2Dゲームカメラの描画範囲を重ねる
			const std::array<Vector3, 4> kNdcCorners = {
				Vector3(-1.0f, 1.0f, 0.0f),
				Vector3(1.0f, 1.0f, 0.0f),
				Vector3(1.0f, -1.0f, 0.0f),
				Vector3(-1.0f, -1.0f, 0.0f),
			};
			const Matrix4x4 inverseViewProjection = Matrix4x4::Inverse(
				context.gameView->orthographic.matrices.viewProjectionMatrix);
			std::array<Vector2, 4> worldCorners{};
			for (size_t i = 0; i < kNdcCorners.size(); ++i) {
				const Vector3 world = Vector3::Transform(kNdcCorners[i], inverseViewProjection);
				worldCorners[i] = Vector2(world.x, world.y);
			}

			LineRenderer2D* renderer = LineRenderer::GetInstance()->Get2D();
			for (size_t i = 0; i < worldCorners.size(); ++i) {
				renderer->DrawLine(worldCorners[i], worldCorners[(i + 1) % worldCorners.size()],
					Color4::Yellow(), 2.0f);
			}
		}

		// スナップグリッドだけは不透明メッシュに隠れるよう、シーン深度を渡して深度テストさせる
		DepthTexture2D* sceneDepth = (context.resources && context.resources->GetSceneMain()) ?
			context.resources->GetSceneMain()->GetDepthTexture() : nullptr;
		LineRenderer::GetInstance()->RenderSceneView(
			graphicsCore, *context.view, *context.defaultSurface, context.drawSceneViewDefaultGrid, true, sceneDepth);
	}
#endif
}
