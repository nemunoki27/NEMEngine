#include "PendingComponent.h"

//============================================================================
//	PendingComponent classMethods
//============================================================================
Engine::PendingComponent::PendingComponent(const ComponentTypeInfo& info, uint64_t instanceID) :
	info_(info), instanceID_(instanceID), value_(std::make_shared<ValueState>(info)) {
}

Engine::PendingComponent::ValueState::ValueState(const ComponentTypeInfo& info) :
	info(info), data(info.size, info.align) {

	// 外部データの初期化はWorldへ追加するまで保留する
	info.constructDefault(data.GetData());
}

Engine::PendingComponent::ValueState::~ValueState() {

	info.destroy(data.GetData());
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
	// 借用がなければその場で値を解放する
	value_.reset();
}
