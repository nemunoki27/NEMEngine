#include "GraphicsFrameResources.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>
#include <utility>

//============================================================================
//	GraphicsFrameResources classMethods
//============================================================================

Engine::GraphicsFrameResourceRegistry::~GraphicsFrameResourceRegistry() {

	// 管理側の終了後へ接続を残さない
	while (first_) first_->Reset();
}

void Engine::GraphicsFrameResourceRegistry::ReleaseInactive(uint32_t count) {

	if (count < 1 || count > 2) throw std::invalid_argument("描画フレーム数が範囲外です");
	++revision_;
	// 子資源の破棄で登録集合が変わっても生存中の通知先を探し直す
	for (;;) {
		auto* entry = first_;
		while (entry && entry->visitedRevision_ == revision_) entry = entry->next_;
		if (!entry) break;
		entry->visitedRevision_ = revision_;
		entry->release_(entry->owner_, count);
	}
}

Engine::GraphicsFrameResourceRegistration::~GraphicsFrameResourceRegistration() {

	Reset();
}

void Engine::GraphicsFrameResourceRegistration::Bind(GraphicsFrameResourceRegistry& registry,
	void* owner, ReleaseFunction release, PrepareFunction prepare, ReleaseFunction cancel) {

	if (registry_ == &registry) {
		owner_ = owner;
		release_ = release;
		prepare_ = prepare;
		cancel_ = cancel;
		return;
	}
	Reset();
	registry_ = &registry;
	owner_ = owner;
	release_ = release;
	prepare_ = prepare;
	cancel_ = cancel;
	visitedRevision_ = registry.revision_;
	next_ = registry.first_;
	if (next_) next_->previous_ = this;
	registry.first_ = this;
}

void Engine::GraphicsFrameResourceRegistration::Reset() {

	if (registry_) {
		if (previous_) previous_->next_ = next_;
		else registry_->first_ = next_;
		if (next_) next_->previous_ = previous_;
	}
	registry_ = nullptr;
	previous_ = nullptr;
	next_ = nullptr;
	owner_ = nullptr;
	release_ = nullptr;
	prepare_ = nullptr;
	cancel_ = nullptr;
}

void Engine::GraphicsFrameResourceRegistration::Swap(GraphicsFrameResourceRegistration& other,
	void* owner, void* otherOwner) noexcept {

	// リスト上の位置ではなく接続内容を交換する
	auto* registry = registry_;
	auto release = release_;
	auto prepare = prepare_;
	auto cancel = cancel_;
	auto* otherRegistry = other.registry_;
	auto otherRelease = other.release_;
	auto otherPrepare = other.prepare_;
	auto otherCancel = other.cancel_;
	Reset();
	other.Reset();
	if (otherRegistry) Bind(*otherRegistry, owner, otherRelease, otherPrepare, otherCancel);
	if (registry) other.Bind(*registry, otherOwner, release, prepare, cancel);
}

bool Engine::GraphicsFrameResourceRegistry::Prepare(uint32_t count) {

	if (count < 1 || count > 2) throw std::invalid_argument("描画フレーム数が範囲外です");
	try {
		for (auto* entry = first_; entry; entry = entry->next_) {
			if (entry->prepare_ && !entry->prepare_(entry->owner_, count)) {
				Cancel(count);
				return false;
			}
		}
	} catch (...) {
		Cancel(count);
		throw;
	}
	return true;
}

void Engine::GraphicsFrameResourceRegistry::Cancel(uint32_t count) {

	for (auto* entry = first_; entry; entry = entry->next_) {
		if (entry->cancel_) entry->cancel_(entry->owner_, count);
	}
}
