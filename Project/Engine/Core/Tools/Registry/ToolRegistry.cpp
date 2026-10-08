#include "ToolRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <exception>

//============================================================================
//	ToolRegistry classMethods
//============================================================================
Engine::ToolRegistry::~ToolRegistry() {

	ClearNoThrow();
}

void Engine::ToolRegistry::ClearNoThrow() noexcept {

	try {
		Clear();
	} catch (...) {

		// 終了通知の失敗を診断して破棄を続ける
		try {
			Logger::Output(LogType::Engine, spdlog::level::err, "Toolの終了通知に失敗しました");
		} catch (...) {
			// 診断の例外を破棄処理から出さない
		}
	}
}

bool Engine::ToolRegistry::Register(std::unique_ptr<ITool> tool) {

	if (!tool || clearing_) {
		return false;
	}

	const std::string id = tool->GetDescriptor().id;
	if (id.empty() || idToIndex_.find(id) != idToIndex_.end()) {
		return false;
	}

	// 登録通知中の重複登録を防ぎ、対象の寿命を保持する
	const std::shared_ptr<ITool> registered = std::move(tool);
	tools_.emplace_back(registered);
	SortTools();
	RebuildIndex();
	try {
		registered->OnRegistered();
	} catch (...) {

		// 同じIDで登録された別のツールを巻き戻さない
		std::erase(tools_, registered);
		RebuildIndex();
		throw;
	}
	return Find(id) == registered.get();
}

bool Engine::ToolRegistry::Unregister(std::string_view id) {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return false;
	}

	const uint32_t index = it->second;
	// 検索から外してから解除通知を送る
	const auto removed = tools_[index];
	tools_.erase(tools_.begin() + index);
	RebuildIndex();
	removed->OnUnregistered();
	return true;
}

void Engine::ToolRegistry::Clear() {

	if (clearing_) {
		return;
	}
	// 全対象を検索から外し、終了中の再登録を止める
	clearing_ = true;
	auto removed = std::move(tools_);
	tools_.clear();
	idToIndex_.clear();
	std::exception_ptr failure;
	for (const auto& tool : removed) {
		try {
			tool->OnUnregistered();
		} catch (...) {

			// 1件の終了失敗で残りの通知を止めない
			if (!failure) failure = std::current_exception();
		}
	}
	removed.clear();
	clearing_ = false;
	if (failure) {
		std::rethrow_exception(failure);
	}
}

void Engine::ToolRegistry::Tick(ToolContext& context) {

	// 更新中に登録配列が変わっても走査と寿命を保つ
	const auto snapshot = GetToolSnapshot();
	for (const auto& tool : snapshot) {
		const std::string id = tool->GetDescriptor().id;
		if (Find(id) != tool.get() || !tool->IsEnabled(context)) {
			continue;
		}
		if (Find(id) == tool.get()) {
			tool->Tick(context);
		}
	}
}

Engine::ITool* Engine::ToolRegistry::Find(std::string_view id) {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return nullptr;
	}
	return tools_[it->second].get();
}

const Engine::ITool* Engine::ToolRegistry::Find(std::string_view id) const {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return nullptr;
	}
	return tools_[it->second].get();
}

std::vector<Engine::ITool*> Engine::ToolRegistry::GetTools() {

	std::vector<ITool*> result;
	result.reserve(tools_.size());
	for (auto& tool : tools_) {
		result.emplace_back(tool.get());
	}
	return result;
}

std::vector<const Engine::ITool*> Engine::ToolRegistry::GetTools() const {

	std::vector<const ITool*> result;
	result.reserve(tools_.size());
	for (const auto& tool : tools_) {
		result.emplace_back(tool.get());
	}
	return result;
}

std::shared_ptr<Engine::ITool> Engine::ToolRegistry::Acquire(std::string_view id) {

	const auto found = idToIndex_.find(std::string(id));
	return found == idToIndex_.end() ? nullptr : tools_[found->second];
}

std::vector<std::shared_ptr<Engine::ITool>> Engine::ToolRegistry::GetToolSnapshot() {

	return tools_;
}

Engine::ToolRegistry& Engine::ToolRegistry::GetInstance() {

	static ToolRegistry registry;
	return registry;
}

void Engine::ToolRegistry::RebuildIndex() {

	// 現在の並びに検索位置を揃える
	idToIndex_.clear();
	for (uint32_t i = 0; i < static_cast<uint32_t>(tools_.size()); ++i) {
		idToIndex_[tools_[i]->GetDescriptor().id] = i;
	}
}

void Engine::ToolRegistry::SortTools() {

	// カテゴリと表示順で並べ、同順位はIDで揃える
	std::sort(tools_.begin(), tools_.end(),
		[](const std::shared_ptr<ITool>& lhs, const std::shared_ptr<ITool>& rhs) {

			const ToolDescriptor& lhsDesc = lhs->GetDescriptor();
			const ToolDescriptor& rhsDesc = rhs->GetDescriptor();
			if (lhsDesc.category != rhsDesc.category) {
				return lhsDesc.category < rhsDesc.category;
			}
			if (lhsDesc.order != rhsDesc.order) {
				return lhsDesc.order < rhsDesc.order;
			}
			return lhsDesc.id < rhsDesc.id;
		});
}
