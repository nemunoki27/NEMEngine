#include "PendingComponent.h"

//============================================================================
//	PendingComponent classMethods
//============================================================================
Engine::PendingComponent::PendingComponent(const ComponentTypeInfo& info, uint64_t instanceID) :
	info_(info), instanceID_(instanceID), data_(info.size, info.align) {

	// 外部データの初期化はWorldへ追加するまで保留する
	info_.constructDefault(data_.ptr);
}

Engine::PendingComponent::~PendingComponent() {

	Cancel();
}

void Engine::PendingComponent::Cancel() {

	if (instanceID_ == 0) {
		return;
	}
	// 再追加された個体へ古い予約を接続しない
	instanceID_ = 0;
	info_.destroy(data_.ptr);
	data_.Release();
}
