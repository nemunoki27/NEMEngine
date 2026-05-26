#include "PostProcessTemporaryTargetPool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>

// c++
#include <algorithm>
#include <utility>

//============================================================================
//	PostProcessTemporaryTargetPool classMethods
//============================================================================

Engine::MultiRenderTarget* Engine::PostProcessTemporaryTargetPool::Acquire(
	GraphicsCore& graphicsCore, RenderTargetRegistry& registry,
	const std::string& name, const MultiRenderTarget& source) {

	PostProcessTemporaryTargetDesc desc{};
	desc.name = name;
	if (source.GetColorCount() != 0 && source.GetColorTexture(0)) {
		desc.format = source.GetColorTexture(0)->GetFormat();
	}
	return Acquire(graphicsCore, registry, desc, source);
}

Engine::MultiRenderTarget* Engine::PostProcessTemporaryTargetPool::Acquire(
	GraphicsCore& graphicsCore, RenderTargetRegistry& registry,
	const PostProcessTemporaryTargetDesc& tempDesc, const MultiRenderTarget& source) {

	if (tempDesc.name.empty() || source.GetColorCount() == 0 || !source.GetColorTexture(0)) {
		return nullptr;
	}

	// レンダーターゲットの情報を構築
	SceneRenderTargetDesc desc{};
	desc.name = tempDesc.name;
	desc.sizeMode = SceneRenderTargetSizeMode::Fixed;
	desc.fixedWidth = (std::max)(1u, static_cast<uint32_t>(static_cast<float>(source.GetWidth()) * tempDesc.widthScale));
	desc.fixedHeight = (std::max)(1u, static_cast<uint32_t>(static_cast<float>(source.GetHeight()) * tempDesc.heightScale));
	desc.withDepth = false;
	// 色情報
	SceneRenderTargetColorDesc color{};
	color.name = tempDesc.name;
	color.format = ToSceneFormat(tempDesc.format);
	color.createUAV = tempDesc.createUAV;
	desc.colors.emplace_back(std::move(color));

	// ResizeTransientは同名RTを保持し、サイズ/フォーマットが変わった時だけ作り直す
	return registry.ResizeTransient(graphicsCore, desc, desc.fixedWidth, desc.fixedHeight);
}

Engine::SceneRenderTargetFormat Engine::PostProcessTemporaryTargetPool::ToSceneFormat(DXGI_FORMAT format) {

	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return SceneRenderTargetFormat::RGBA8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return SceneRenderTargetFormat::RGBA16_FLOAT;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	default:
		return SceneRenderTargetFormat::RGBA32_FLOAT;
	}
}
