#include "EditorSelectionScreenSpaceOutlinePass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

// c++
#include <array>
#include <cmath>

//============================================================================
//	EditorSelectionScreenSpaceOutlinePass classMethods
//============================================================================
void Engine::EditorSelectionScreenSpaceOutlinePass::Execute([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, [[maybe_unused]] SceneExecutionContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// 選択アウトラインはSceneViewのEditor描画専用
	if (!context.resources || context.kind != RenderViewKind::Scene) {
		return;
	}

	// 最初の選択操作でマスクと合成のPSOを同期生成しないようにする
	auto* main = context.resources->GetSceneMain();
	auto* final = context.resources->GetSceneFinal();
	if (!pipelinesPrepared_ && main && final && final->GetColorTexture(0) && deps_.pipelineCache && deps_.assetLibrary) {
		const auto mask = RenderPathResources::BuildScreenSpaceOutlineMaskDesc(1, 1, {}, false);
		const std::array maskFormats{ mask.colors.front().format };
		const std::array finalFormats{ final->GetColorTexture(0)->GetFormat() };
		auto* depth = context.outputDepth ? context.outputDepth : main->GetDepthTexture();
		for (AssetID id : { BuiltinAssets::Materials::ScreenSpaceOutlineMask, BuiltinAssets::Materials::ScreenSpaceOutlineDilate,
				BuiltinAssets::Materials::ScreenSpaceOutlineComposite }) {
			const auto* material = deps_.assetLibrary->LoadMaterial(id);
			if (!material) continue;
			for (const auto& pass : material->passes) {
				std::span<const DXGI_FORMAT> formats;
				if (id == BuiltinAssets::Materials::ScreenSpaceOutlineMask) formats = maskFormats;
				if (id == BuiltinAssets::Materials::ScreenSpaceOutlineComposite) formats = finalFormats;
				const auto dsv = pass.passKind == MaterialPassKind::ScreenSpaceOutlineMask && depth ?
					depth->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
				deps_.pipelineCache->GetORCreate(graphicsCore.GetDXObject(), *deps_.assetLibrary,
					pass.pipeline, pass.preferredVariant, formats, dsv);
			}
		}
		pipelinesPrepared_ = true;
	}

	// Editorの選択サービスが積んだ要求から、現worldかつwidthが有効なものだけ拾う
	requests_.clear();
	for (const ScreenSpaceOutlineRequest& request :
		EditorSelectionOutlineRequestService::GetInstance().GetRequests()) {

		if (!request.world || request.world != context.world) {
			continue;
		}
		if (!std::isfinite(request.style.widthPixels) || request.style.widthPixels <= 0.0f) {
			continue;
		}
		requests_.emplace_back(request);
	}
	if (requests_.empty()) {
		return;
	}

	// Editor専用のScreenSpaceOutlineへ描いてruntime用とは分離する
	constexpr std::array<RenderPhase, 2> kEditorOutlinePhases = {
		RenderPhase::Opaque,
		RenderPhase::Transparent,
	};
	DepthTexture2D* depth = context.resources->GetSceneMain() ?
		context.resources->GetSceneMain()->GetDepthTexture() : nullptr;
	if (context.outputDepth) depth = context.outputDepth;
	renderer_.Render(graphicsCore, context, passBuckets, deps_, requests_,
		context.resources->PrepareScreenSpaceOutline(graphicsCore, true),
		kEditorOutlinePhases, context.resources->GetSceneFinal(),
		depth);
#endif
}
