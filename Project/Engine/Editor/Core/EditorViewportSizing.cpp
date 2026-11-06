#include "EditorViewportSizing.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	EditorViewportSizing classMethods
//============================================================================
void Engine::EditorViewportSizing::Update(const Vector2& imageSize, const Vector2& framebufferScale,
	const Vector2I& referenceSize, double time, bool resizing) {

	if (referenceSize.x <= 0 || referenceSize.y <= 0 || !std::isfinite(time) ||
		!std::isfinite(imageSize.x) || !std::isfinite(imageSize.y) ||
		!std::isfinite(framebufferScale.x) || !std::isfinite(framebufferScale.y) ||
		imageSize.x <= 0.0f || imageSize.y <= 0.0f || framebufferScale.x <= 0.0f || framebufferScale.y <= 0.0f) return;
	// 端数と小さな揺れは八画素単位で切り上げ、製品解像度を上限にする
	const auto dimension = [](float size, float scale, int32_t limit) {
		const double pixels = (std::min)(static_cast<double>(size) * scale, static_cast<double>(limit));
		return static_cast<int32_t>((std::min)(std::ceil(pixels / 8.0) * 8.0, static_cast<double>(limit)));
	};
	const Vector2I target{dimension(imageSize.x, framebufferScale.x, referenceSize.x),
		dimension(imageSize.y, framebufferScale.y, referenceSize.y)};
	if (renderSize_.x == 0 || renderSize_.y == 0 || referenceSize_ != referenceSize) {
		referenceSize_ = referenceSize;
		pendingSize_ = renderSize_ = target;
		changedAt_ = time;
		return;
	}
	if (pendingSize_ != target || time < changedAt_) {
		pendingSize_ = target;
		changedAt_ = time;
	}
	// ドッキングやWindowのサイズ変更中は直前の描画先を使い続ける
	if (!resizing && time - changedAt_ >= 0.15) renderSize_ = pendingSize_;
}

Engine::Vector2I Engine::EditorViewportSizing::Resolve(const Vector2I& referenceSize) const {

	if (renderSize_.x <= 0 || renderSize_.y <= 0 || referenceSize_ != referenceSize) return referenceSize;
	return renderSize_;
}
