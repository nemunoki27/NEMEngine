#include "ManagedSourceWatcher.h"

//============================================================================
//	include
//============================================================================
// c++
#include <vector>

//============================================================================
//	ManagedSourceWatcher classMethods
//============================================================================
Engine::ManagedSourceWatcher::~ManagedSourceWatcher() {

	Stop();
}

bool Engine::ManagedSourceWatcher::Start(const std::filesystem::path& directory) {

	// 共通の監視処理へ開始と停止を任せる
	return watcher_.Start(directory);
}

void Engine::ManagedSourceWatcher::Stop() {

	// 通知I/Oが完了するまで監視資源を保持する
	watcher_.Stop();
}

bool Engine::ManagedSourceWatcher::ConsumeChanged() {

	// ファイル通知と再走査要求を変更フラグへまとめる
	std::vector<std::filesystem::path> changes;
	watcher_.DrainChanges(changes);
	return !changes.empty();
}
