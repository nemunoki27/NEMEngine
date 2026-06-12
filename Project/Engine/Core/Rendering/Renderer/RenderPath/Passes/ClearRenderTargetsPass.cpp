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
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// このパスはフレーム冒頭のclear専用でバケットは参照しない
	(void)passBuckets;
	if (!context.resources || !context.resources->IsValid()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	// SceneMainは色と深度を毎フレームclearし前フレームの残像を断つ、深度は1.0で最遠
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTargetClearDesc clearDesc{};
	clearDesc.clearColor = true;
	clearDesc.clearDepth = true;
	clearDesc.clearDepthValue = 1.0f;
	clearDesc.clearStencil = false;

	// RTV書き込み状態へ遷移してからbindしviewportを合わせてclearする
	sceneMain->TransitionForRender(*dxCommand);
	sceneMain->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneMain->GetWidth(), sceneMain->GetHeight());
	sceneMain->Clear(*dxCommand, clearDesc);

	// 出力先サーフェスがあればそちらもclearする、深度textureが無ければ色だけ
	if (context.defaultSurface) {

		MultiRenderTargetClearDesc surfaceClearDesc{};
		surfaceClearDesc.clearColor = true;
		surfaceClearDesc.clearDepth = (context.defaultSurface->GetDepthTexture() != nullptr);
		surfaceClearDesc.clearDepthValue = 1.0f;
		surfaceClearDesc.clearStencil = false;

		context.defaultSurface->TransitionForRender(*dxCommand);
		context.defaultSurface->Bind(*dxCommand);
		dxCommand->SetViewportAndScissor(context.defaultSurface->GetWidth(), context.defaultSurface->GetHeight());
		context.defaultSurface->Clear(*dxCommand, surfaceClearDesc);
	}
}
