#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <dxgiformat.h>
#include <string>
#include <unordered_map>

namespace Engine { class GraphicsCore; class MultiRenderTarget; class RenderTargetRegistry; }

namespace Engine::RenderFeatureResourceUtility {

	std::string MakeStateKey(Engine::RenderViewKind kind,
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
