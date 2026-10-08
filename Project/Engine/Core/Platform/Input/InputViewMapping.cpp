#include "InputViewMapping.h"

using namespace Engine;

void InputViewMapping::SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
	const Vector2& dstSize, const Vector2& srcSize, InputViewCoordinateSpace coordinateSpace) {

	ViewRect& rect = viewRects_[viewArea];
	rect.dstPos = dstPos;
	rect.dstSize = dstSize;
	rect.coordinateSpace = coordinateSpace;
	if (srcSize.x <= 0.0f || srcSize.y <= 0.0f) {
		rect.srcSize = dstSize;
	} else {
		rect.srcSize = srcSize;
	}
}

bool InputViewMapping::HasViewRect(InputViewArea viewArea) const {

	return viewRects_.find(viewArea) != viewRects_.end();
}

bool InputViewMapping::IsMouseOnView(InputViewArea viewArea, const InputDeviceState& state) const {

	auto found = viewRects_.find(viewArea);
	if (found == viewRects_.end()) {
		return false;
	}
	const ViewRect& rect = found->second;
	const Vector2 mouse = rect.coordinateSpace == InputViewCoordinateSpace::Screen ?
		state.mouseScreenPos : state.mousePos;
	return (mouse.x >= rect.dstPos.x && mouse.y >= rect.dstPos.y &&
		mouse.x < rect.dstPos.x + rect.dstSize.x &&
		mouse.y < rect.dstPos.y + rect.dstSize.y);
}

std::optional<Vector2> InputViewMapping::GetMousePosInView(InputViewArea viewArea, const InputDeviceState& state) const {

	if (!IsMouseOnView(viewArea, state)) {
		return std::nullopt;
	}

	ViewRect rect = viewRects_.at(viewArea);
	if (rect.dstSize.x <= 0.0f || rect.dstSize.y <= 0.0f) {
		return std::nullopt;
	}
	const Vector2 mouse = rect.coordinateSpace == InputViewCoordinateSpace::Screen ?
		state.mouseScreenPos : state.mousePos;
	Vector2 local = Vector2(mouse.x - rect.dstPos.x, mouse.y - rect.dstPos.y);
	if (rect.srcSize.x <= 0.0f || rect.srcSize.y <= 0.0f) {
		return local;
	}
	return local * (rect.srcSize / rect.dstSize);
}

Vector2 InputViewMapping::GetMouseMoveValueInView(InputViewArea viewArea, const Vector2& mouseMove) const {

	const auto it = viewRects_.find(viewArea);
	if (it == viewRects_.end()) {
		return mouseMove;
	}

	const ViewRect& rect = it->second;
	if (rect.dstSize.x <= 0.0f || rect.dstSize.y <= 0.0f ||
		rect.srcSize.x <= 0.0f || rect.srcSize.y <= 0.0f) {
		return mouseMove;
	}
	return mouseMove * (rect.srcSize / rect.dstSize);
}
