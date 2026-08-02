#include "ShaderGraphHistory.h"

//============================================================================
//	ShaderGraphHistory classMethods
//============================================================================
void Engine::ShaderGraphHistory::Reset(
	const ShaderGraphAsset& graph) {

	current_ = graph;
	currentState_ = ToJson(graph).dump();
	undo_.clear();
	redo_.clear();
}

bool Engine::ShaderGraphHistory::Commit(
	const ShaderGraphAsset& graph) {

	const std::string state = ToJson(graph).dump();
	if (state == currentState_) {
		return false;
	}
	undo_.emplace_back(std::move(current_));
	if (undo_.size() > kMaximumHistory) {
		undo_.pop_front();
	}
	current_ = graph;
	currentState_ = state;
	redo_.clear();
	return true;
}

bool Engine::ShaderGraphHistory::Undo(
	ShaderGraphAsset& outGraph) {

	if (undo_.empty()) {
		return false;
	}
	redo_.emplace_back(std::move(current_));
	current_ = std::move(undo_.back());
	undo_.pop_back();
	currentState_ = ToJson(current_).dump();
	outGraph = current_;
	return true;
}

bool Engine::ShaderGraphHistory::Redo(
	ShaderGraphAsset& outGraph) {

	if (redo_.empty()) {
		return false;
	}
	undo_.emplace_back(std::move(current_));
	current_ = std::move(redo_.back());
	redo_.pop_back();
	currentState_ = ToJson(current_).dump();
	outGraph = current_;
	return true;
}
