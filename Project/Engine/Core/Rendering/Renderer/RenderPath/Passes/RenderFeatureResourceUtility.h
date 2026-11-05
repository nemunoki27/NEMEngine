#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <dxgiformat.h>
#include <string>
#include <unordered_map>

namespace Engine { class GraphicsCore; class MultiRenderTarget; class RenderTargetRegistry; struct SceneExecutionContext; struct RenderPipelineDeps; struct RenderPassPhaseBuckets; }

namespace Engine::RenderFeatureResourceUtility {

	// 後段の独自描画がScene属性のスナップショットを使うか調べる
	bool RequiresSceneColorSnapshot(const SceneExecutionContext& context,
		const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps);

	// 入出力の解像度と形式に合わせてシーン色を転送する
	bool CopySceneOutput(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const RenderPipelineDeps& deps, const RenderFeatureOutputReference& output,
		MultiRenderTarget* source, MultiRenderTarget* destination, const std::string& destinationAlias);

	std::string MakeStateKey(const Engine::ResolvedRenderView& view,
		Engine::UUID passID, std::string_view output = {});

	DXGI_FORMAT ToDXGIFormat(
		Engine::RenderFeatureTextureFormat format,
		DXGI_FORMAT inheritedFormat);

	std::string MakeOutputAlias(
		const Engine::RenderFeatureOutputReference& reference);

	Engine::RenderFeatureOutputSettings GetPrimaryOutput(
		const Engine::RenderFeaturePassSettings& pass);

	Engine::MultiRenderTarget* ResolveOutputTarget(
		Engine::RenderTargetRegistry& registry,
		const Engine::RenderFeatureOutputReference& reference);

	void ApplyDefaultSceneInputs(
		std::unordered_map<std::string, std::string>& inputs);

	void ClearOutputTargets(Engine::GraphicsCore& graphicsCore,
		const std::unordered_map<std::string,
			Engine::MultiRenderTarget*>& outputTargets);

	bool CanCopyColor(const Engine::MultiRenderTarget* source,
		const Engine::MultiRenderTarget* destination);
}
