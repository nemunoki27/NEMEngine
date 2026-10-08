#include "PostProcessTemporaryTargetPool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetSizing.h>

// c++
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

	// 整数変換前にサイズを検証する
	auto size = RenderTargetSizing::ResolveSize(source.GetWidth(), source.GetHeight(),
		tempDesc.widthScale, tempDesc.heightScale);
	if (!size) {
		return nullptr;
	}

	// レンダーターゲットの情報を構築
	SceneRenderTargetDesc desc{};
	desc.name = tempDesc.name;
	desc.sizeMode = SceneRenderTargetSizeMode::Fixed;
	desc.fixedWidth = size->width;
	desc.fixedHeight = size->height;
	desc.withDepth = false;
	// 色情報
	SceneRenderTargetColorDesc color{};
	color.name = tempDesc.name;
	color.format = ToSceneFormat(tempDesc.format);
	color.createUAV = tempDesc.createUAV;
	desc.colors.emplace_back(std::move(color));

	// 同名RTを保持し、サイズ・形式の変更時だけ作り直す
	return registry.ResizeTransient(graphicsCore, desc, desc.fixedWidth, desc.fixedHeight);
}

Engine::SceneRenderTargetFormat Engine::PostProcessTemporaryTargetPool::ToSceneFormat(DXGI_FORMAT format) {

	switch (format) {
	case DXGI_FORMAT_R8_UNORM:
		return SceneRenderTargetFormat::R8_UNORM;
	case DXGI_FORMAT_R16_FLOAT:
		return SceneRenderTargetFormat::R16_FLOAT;
	case DXGI_FORMAT_R16G16_FLOAT:
		return SceneRenderTargetFormat::RG16_FLOAT;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return SceneRenderTargetFormat::RGBA8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return SceneRenderTargetFormat::RGBA16_FLOAT;
	case DXGI_FORMAT_R32_FLOAT:
		return SceneRenderTargetFormat::R32_FLOAT;
	case DXGI_FORMAT_R32G32_FLOAT:
		return SceneRenderTargetFormat::RG32_FLOAT;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	default:
		return SceneRenderTargetFormat::RGBA32_FLOAT;
	}
}
