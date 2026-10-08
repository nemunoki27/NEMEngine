#include "ManagedScriptExceptionStore.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptExceptionParser.h"
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <ctime>

namespace {

	// 報告時刻をHH:MM:SSで取得
	std::string NowTimeString() {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char buffer[16]{};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
		return std::string(buffer);
	}

}

Engine::Entity Engine::ManagedScriptException::ResolveOwner(const ECSWorld& world) const {

	// WorldとEntityの世代が一致する場合だけ選択する
	const Entity owner{entityIndex, entityGeneration};
	const ManagedWorldHandle handle{worldIndex, worldGeneration};
	if (!handle.IsValid() || ManagedWorldRegistry::GetInstance().TryResolve(handle) != &world) {
		return Entity::Null();
	}
	return owner.IsValid() && world.IsAlive(owner) ? owner : Entity::Null();
}

void Engine::ManagedScriptExceptionStore::ReportJSON(const char* jsonUTF8) {

	// 壊れた診断を履歴へ追加しない
	ManagedScriptException entry{};
	if (!ParseManagedScriptException(jsonUTF8, entry)) {
		return;
	}
	entry.exceptionID = nextID_;
	entry.timestamp = NowTimeString();
	entries_.push_back(std::move(entry));
	++nextID_;
	ReportFailure();
	EnforceBounds();
	++version_;
}

void Engine::ManagedScriptExceptionStore::ReportFailure() noexcept {

	// 履歴の削除では失敗通知を巻き戻さない
	++reportSequence_;
}

void Engine::ManagedScriptExceptionStore::Clear() {

	if (entries_.empty()) {
		return;
	}
	entries_.clear();
	++version_;
}

void Engine::ManagedScriptExceptionStore::EnforceBounds() {

	// 最新の例外を優先して残す
	while (entries_.size() > kMaxEntries) {
		entries_.pop_front();
	}
}

Engine::ManagedScriptExceptionStore& Engine::ManagedScriptExceptionStore::GetInstance() {

	static ManagedScriptExceptionStore store;
	return store;
}
