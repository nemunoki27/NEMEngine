#include "ClearRenderTargetsPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	ClearRenderTargetsPass classMethods
//============================================================================

void Engine::ClearRenderTargetsPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.resources->IsValid()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	// 色と深度のクリア
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTargetClearDesc clearDesc{};
	clearDesc.clearColor = true;
	clearDesc.clearDepth = true;
	clearDesc.clearDepthValue = 1.0f;
	clearDesc.clearStencil = true;
	// RTV書き込み状態へ遷移してからクリア
	sceneMain->TransitionForRender(*dxCommand);
	sceneMain->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneMain->GetWidth(), sceneMain->GetHeight());
	sceneMain->Clear(*dxCommand, clearDesc);
	// 出力先サーフェスがあればそちらもクリアする
	if (context.defaultSurface && context.clearDefaultSurface) {

		// サーフェイス設定
		MultiRenderTargetClearDesc surfaceClearDesc{};
		surfaceClearDesc.clearColor = true;
		surfaceClearDesc.clearColorValue = graphicsCore.GetContext().GetGraphicsSetting().sceneClearColor;
		surfaceClearDesc.clearDepth = (context.defaultSurface->GetDepthTexture() != nullptr);
		surfaceClearDesc.clearDepthValue = 1.0f;
		const auto* surfaceDepth = context.defaultSurface->GetDepthTexture();
		surfaceClearDesc.clearStencil = surfaceDepth &&
			(surfaceDepth->GetDSVFormat() == DXGI_FORMAT_D24_UNORM_S8_UINT ||
				surfaceDepth->GetDSVFormat() == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);

		context.defaultSurface->TransitionForRender(*dxCommand);
		context.defaultSurface->Bind(*dxCommand);
		dxCommand->SetViewportAndScissor(context.defaultSurface->GetWidth(), context.defaultSurface->GetHeight());
		context.defaultSurface->Clear(*dxCommand, surfaceClearDesc);
	}
}
