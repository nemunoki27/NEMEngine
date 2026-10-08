#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetSizing.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <cmath>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

	// 読み取り専用の所有元から子資源を変更させない
	static_assert(std::is_same_v<decltype(std::declval<Engine::MultiRenderTarget&>().GetColorTexture(0)),
		Engine::RenderTexture2D*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::MultiRenderTarget&>().GetColorTexture(0)),
		const Engine::RenderTexture2D*>);
	static_assert(std::is_same_v<decltype(std::declval<Engine::MultiRenderTarget&>().GetDepthTexture()),
		Engine::DepthTexture2D*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::MultiRenderTarget&>().GetDepthTexture()),
		const Engine::DepthTexture2D*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::RenderPathResources&>().GetSceneMain()),
		const Engine::MultiRenderTarget*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::RenderPathResources&>().GetGBufferAlbedo()),
		const Engine::RenderTexture2D*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ScreenSpaceOutlineViewResources&>().GetMask()),
		const Engine::MultiRenderTarget*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ScreenSpaceOutlineViewResources&>().GetProjectedCoverageMask()),
		const Engine::MultiRenderTarget*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ScreenSpaceOutlineViewResources&>().GetHorizontalDilatedMask()),
		const Engine::MultiRenderTarget*>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ScreenSpaceOutlineViewResources&>().GetDilatedMask()),
		const Engine::MultiRenderTarget*>);
}

bool NEMTests::TestRenderTargetSizing() {

	using Engine::RenderTargetSizing::ResolveSize;
	// 単精度での丸めと最小サイズを維持する
	auto scaled = ResolveSize(1920, 1080, 0.7f, 0.5f);
	auto minimum = ResolveSize(0, 1, 1.0f, 0.0001f);
	auto limit = ResolveSize(16384, 16384);
	if (!scaled || scaled->width != 1344 || scaled->height != 540 ||
		!minimum || minimum->width != 1 || minimum->height != 1 ||
		!limit || limit->width != 16384 || limit->height != 16384) {

		return false;
	}

	// 不正な倍率はどちらの軸でも拒否する
	for (float scale : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {

		if (ResolveSize(1920, 1080, scale, 1.0f) || ResolveSize(1920, 1080, 1.0f, scale)) {
			return false;
		}
	}
	// GPUの上限超過と浮動小数点の桁あふれを拒否する
	if (ResolveSize(16385, 1) || ResolveSize(1, 16385) ||
		ResolveSize(16384, 1, std::nextafter(1.0f, 2.0f)) ||
		ResolveSize(std::numeric_limits<uint32_t>::max(), 1) ||
		ResolveSize(2, 1, std::numeric_limits<float>::max())) {

		return false;
	}

	// 不正なサイズでは登録済みの描画先とGPUに触れない
	Engine::GraphicsCore graphicsCore;
	Engine::MultiRenderTarget surface;
	const Engine::MultiRenderTarget& readOnly = surface;
	Engine::RenderPathResources resources;
	const Engine::RenderPathResources& readOnlyResources = resources;
	if (surface.GetColorTexture(0) || readOnly.GetColorTexture(0) || readOnly.GetDepthTexture() ||
		resources.GetGBuffer(Engine::GBufferAttachment::Count) || readOnlyResources.GetGBufferAlbedo() ||
		readOnlyResources.GetRuntimeScreenSpaceOutline().GetMask() ||
		readOnlyResources.GetEditorSelectionScreenSpaceOutline().GetDilatedMask()) {

		return false;
	}
	Engine::RenderTargetRegistry registry;
	registry.Register("Guarded", &surface, {"Color"}, std::nullopt);
	Engine::SceneRenderTargetDesc desc{};
	desc.name = "Guarded";
	desc.widthScale = std::numeric_limits<float>::quiet_NaN();
	if (registry.ResizeTransient(graphicsCore, desc, 1920, 1080) || registry.Find("Guarded") != &surface) {
		return false;
	}
	desc.sizeMode = Engine::SceneRenderTargetSizeMode::Fixed;
	desc.fixedWidth = 16385;
	desc.fixedHeight = 1;
	return !registry.ResizeTransient(graphicsCore, desc, 1920, 1080) && registry.Find("Guarded") == &surface;
}
