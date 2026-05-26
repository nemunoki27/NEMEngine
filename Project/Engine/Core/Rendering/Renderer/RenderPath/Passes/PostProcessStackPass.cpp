#include "PostProcessStackPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

//============================================================================
//	PostProcessStackPass classMethods
//============================================================================

namespace {

	constexpr const char* kSceneColorFinal = "SceneColorFinal";
	constexpr const char* kPingName = "PostProcessPing";
	constexpr const char* kPongName = "PostProcessPong";

	bool CopyColor0Resource(Engine::GraphicsCore& graphicsCore,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest) {

		if (!source || !dest) {
			return false;
		}
		Engine::RenderTexture2D* sourceColor = source->GetColorTexture(0);
		Engine::RenderTexture2D* destColor = dest->GetColorTexture(0);
		if (!sourceColor || !destColor) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_DEST);
		dxCommand->GetCommandList()->CopyResource(destColor->GetResource(), sourceColor->GetResource());
		dest->TransitionForShaderRead(*dxCommand);
		return true;
	}
}

void Engine::PostProcessStackPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
	if (!context.resources || !context.targetRegistry ||
		!deps_.postProcessExecutor || !deps_.postProcessTargetPool ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneFinal) {
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();
	const PostProcessStackRuntime& runtime = service.GetRuntime();

	if (!runtime.HasEnabledPasses()) {
		return;
	}

	// 有効なパスだけ抽出
	std::vector<const PostProcessStackRuntimePass*> activePasses;
	activePasses.reserve(runtime.passes.size());
	for (const auto& pass : runtime.passes) {
		if (pass.enabled && pass.material) {
			activePasses.push_back(&pass);
		}
	}
	if (activePasses.empty()) {
		return;
	}

	// SceneFinalを入力にするため、1パスだけでも一時RTを必ず経由する
	MultiRenderTarget* ping = deps_.postProcessTargetPool->Acquire(graphicsCore,
		*context.targetRegistry, kPingName, *sceneFinal);
	MultiRenderTarget* pong = nullptr;
	if (activePasses.size() > 2) {
		pong = deps_.postProcessTargetPool->Acquire(graphicsCore, *context.targetRegistry, kPongName, *sceneFinal);
	}
	if (!ping || (activePasses.size() > 2 && !pong)) {
		return;
	}

	// 実行前にリフレクション情報をキャッシュしておく（実行成功に依存しないUIのため）
	for (const auto* passPtr : activePasses) {

		// シェーダーリロード要求があれば、パイプラインとレイアウトキャッシュを破棄する
		if (service.TakeReloadRequest(passPtr->material)) {
			const MaterialAsset* mat = deps_.assetLibrary->LoadMaterial(passPtr->material);
			if (mat) {
				for (const auto& passBinding : mat->passes) {
					deps_.pipelineCache->InvalidateByPipelineAsset(passBinding.pipeline);
				}
			}
			deps_.postProcessExecutor->ClearParameterLayoutCache();
			service.ClearReflection(passPtr->material);
		}

		if (service.FindReflectionVars(passPtr->material) != nullptr) {
			continue;
		}
		std::vector<ShaderConstantBufferVariable> vars;
		std::vector<ShaderResourceBinding> srvs;
		if (deps_.postProcessExecutor->TryGetReflection(graphicsCore, *deps_.assetLibrary,
			*deps_.pipelineCache, passPtr->material, passPtr->passName, vars, srvs)) {
			service.CacheReflection(passPtr->material, vars, srvs);
		}
	}

	const size_t passCount = activePasses.size();
	for (size_t i = 0; i < passCount; ++i) {

		const PostProcessStackRuntimePass& pass = *activePasses[i];
		const bool isFirst = (i == 0);
		const bool isLast = (i == passCount - 1);

		const char* sourceName = nullptr;
		const char* destName = nullptr;
		if (isFirst) {
			sourceName = kSceneColorFinal;
			destName = kPingName;
		} else {
			sourceName = (i % 2 == 1) ? kPingName : kPongName;
			destName = isLast ? kSceneColorFinal : ((i % 2 == 1) ? kPongName : kPingName);
		}

		PostProcessExecutionDesc desc{};
		desc.material = pass.material;
		desc.passName = pass.passName;
		desc.source.colors = { sourceName };
		desc.dest.colors = { destName };
		desc.parameterOverrides = pass.parameterOverrides;
		desc.textureOverrides = pass.textureGuids;
		desc.dispatchMode = ComputeDispatchMode::FromDestSize;

		if (!deps_.postProcessExecutor->Execute(graphicsCore, RenderFrameRequest{},
			context, *deps_.assetLibrary, *deps_.pipelineCache, desc)) {

			// いずれかのpassが失敗したらSceneFinalの既存内容を保持して中断する
			Logger::Output(LogType::Engine, "[PostProcessStack] pass '{}' failed. Aborting stack.", pass.name);
			return;
		}

		// エディタUI用にリフレクション情報をキャッシュする
		const PostProcessParameterLayout* layout = deps_.postProcessExecutor->GetLastExecutedLayout();
		if (layout) {
			service.CacheReflection(pass.material,
				layout->GetVariables(),
				deps_.postProcessExecutor->GetLastExecutedSRVBindings());
		}
	}

	if (passCount == 1) {
		CopyColor0Resource(graphicsCore, ping, sceneFinal);
	}
}
