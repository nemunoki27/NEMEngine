#include "InputWindowEvents.h"

using namespace Engine;

// windows
#include <Windows.h>

void InputWindowEvents::CommitText() {

	// 文字入力を確定し直前のmessage pumpで溜めたWM_CHAR分をframe-localテキストにする
	if (!pendingWide_.empty()) {
		const int needed = ::WideCharToMultiByte(CP_UTF8, 0, pendingWide_.c_str(),
			static_cast<int>(pendingWide_.size()), nullptr, 0, nullptr, nullptr);
		if (needed > 0) {
			frameText_.resize(static_cast<size_t>(needed));
			::WideCharToMultiByte(CP_UTF8, 0, pendingWide_.c_str(), static_cast<int>(pendingWide_.size()),
				frameText_.data(), needed, nullptr, nullptr);
		} else {
			frameText_.clear();
		}
		pendingWide_.clear();
	} else {
		frameText_.clear();
	}

}

void InputWindowEvents::PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint) {

	if (paths.empty()) { return; }
	droppedFiles_ = paths;
	droppedFilesPoint_ = screenPoint;
	hasDroppedFiles_ = true;
}

bool InputWindowEvents::TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) {

	if (!hasDroppedFiles_) { return false; }
	outPaths = std::move(droppedFiles_);
	outClientPoint = droppedFilesPoint_;
	droppedFiles_.clear();
	hasDroppedFiles_ = false;
	return true;
}

bool InputWindowEvents::PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const {

	if (!hasDroppedFiles_) { return false; }
	outPaths = droppedFiles_;
	outClientPoint = droppedFilesPoint_;
	return true;
}
