#include "RenderTargetSizing.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
// directX
#include <d3d12.h>

namespace {

	// 整数変換前に倍率とGPUの上限を検証する
	std::optional<uint32_t> ResolveDimension(uint32_t size, float scale) {

		if (!std::isfinite(scale) || scale <= 0.0f) {
			return std::nullopt;
		}

		// 既存の単精度での丸め方を維持する
		float scaled = static_cast<float>(size) * scale;
		if (!std::isfinite(scaled) || scaled > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
			return std::nullopt;
		}
		return (std::max)(1u, static_cast<uint32_t>(scaled));
	}
}

//============================================================================
//	RenderTargetSizing functions
//============================================================================
std::optional<Engine::RenderTargetSize> Engine::RenderTargetSizing::ResolveSize(
	uint32_t width, uint32_t height, float widthScale, float heightScale) {

	// 両方の寸法が有効な場合だけ公開する
	auto resolvedWidth = ResolveDimension(width, widthScale);
	auto resolvedHeight = ResolveDimension(height, heightScale);
	if (!resolvedWidth || !resolvedHeight) {
		return std::nullopt;
	}
	return RenderTargetSize{*resolvedWidth, *resolvedHeight};
}
