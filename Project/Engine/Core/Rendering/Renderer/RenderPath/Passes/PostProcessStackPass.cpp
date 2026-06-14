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

// c++
#include <cstring>

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

	// エディタの選択中パスを基準に、そのパス実行前後の結果をプレビューへ退避する
	// GameViewの結果のみを対象にすることで、ビューごとにサイズが異なっても
	// プレビュー用一時RTが再生成され続けるのを防ぐ
	const UUID previewPassId = service.GetPreviewPassId();
	const bool capturePreview = (context.kind == RenderViewKind::Game) && static_cast<bool>(previewPassId);
	MultiRenderTarget* previewBefore = nullptr;
	MultiRenderTarget* previewAfter = nullptr;
	bool previewCaptured = false;
	if (capturePreview) {
		previewBefore = deps_.postProcessTargetPool->Acquire(graphicsCore,
			*context.targetRegistry, "PostProcessPreviewBefore", *sceneFinal);
		previewAfter = deps_.postProcessTargetPool->Acquire(graphicsCore,
			*context.targetRegistry, "PostProcessPreviewAfter", *sceneFinal);
	}

	// 実行前にリフレクション情報をキャッシュしておく
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
			*deps_.pipelineCache, passPtr->material, passPtr->passKind, vars, srvs)) {
			service.CacheReflection(passPtr->material, vars, srvs);
		}
	}

	// パスのsource/dest名から、対応する中間RTを引く(プレビュー退避用)
	auto resolveTargetByName = [&](const char* name) -> MultiRenderTarget* {
		if (!name) {
			return nullptr;
		}
		if (std::strcmp(name, kSceneColorFinal) == 0) {
			return sceneFinal;
		}
		if (std::strcmp(name, kPingName) == 0) {
			return ping;
		}
		if (std::strcmp(name, kPongName) == 0) {
			return pong;
		}
		return nullptr;
	};

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

		// 選択中パスなら、実行前のsource内容をbeforeへ退避する
		const bool isPreviewTarget = capturePreview && previewBefore && previewAfter &&
			(pass.id == previewPassId);
		if (isPreviewTarget) {
			CopyColor0Resource(graphicsCore, resolveTargetByName(sourceName), previewBefore);
		}

		PostProcessExecutionDesc desc{};
		desc.material = pass.material;
		desc.passKind = pass.passKind;
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
		const MaterialParameterLayout* layout = deps_.postProcessExecutor->GetLastExecutedLayout();
		if (layout) {
			service.CacheReflection(pass.material,
				layout->GetVariables(),
				deps_.postProcessExecutor->GetLastExecutedSRVBindings());
		}

		// 選択中パスなら、実行後のdest内容をafterへ退避する
		if (isPreviewTarget) {
			CopyColor0Resource(graphicsCore, resolveTargetByName(destName), previewAfter);
			previewCaptured = true;
		}
	}

	if (passCount == 1) {
		CopyColor0Resource(graphicsCore, ping, sceneFinal);
	}

	// 選択中パスの実行前後(before/after)のSRVをサービスへ渡す
	// 退避先はCopyColor0Resource内でシェーダー読み取り状態へ遷移済み
	if (previewCaptured) {

		RenderTexture2D* beforeColor = previewBefore->GetColorTexture(0);
		RenderTexture2D* afterColor = previewAfter->GetColorTexture(0);
		PostProcessStackService::PreviewImage preview{};
		preview.beforeSrvPtr = beforeColor ? beforeColor->GetSRVGPUHandle().ptr : 0;
		preview.afterSrvPtr = afterColor ? afterColor->GetSRVGPUHandle().ptr : 0;
		preview.width = previewAfter->GetWidth();
		preview.height = previewAfter->GetHeight();
		preview.valid = (preview.beforeSrvPtr != 0 && preview.afterSrvPtr != 0);
		service.SetPreviewImage(preview);
	}
}
