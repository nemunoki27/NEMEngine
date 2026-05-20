#include "GraphValidationTypes.h"

//============================================================================
//	GraphValidationResult classMethods
//============================================================================

bool Engine::GraphValidationResult::HasError() const {

	// Apply可否はErrorの有無だけで決める
	for (const GraphValidationMessage& message : messages) {
		if (message.severity == GraphValidationMessage::Severity::Error) {
			return true;
		}
	}
	return false;
}

void Engine::GraphValidationResult::Clear() {

	// Validator実行前に古い結果を破棄する
	messages.clear();
}

void Engine::GraphValidationResult::Add(GraphValidationMessage::Severity severity,
	GraphID nodeID, GraphID pinID, const std::string& message) {

	// 表示側でNode / PinへジャンプできるようにIDも保持する
	messages.push_back(GraphValidationMessage{
		.severity = severity,
		.nodeID = nodeID,
		.pinID = pinID,
		.message = message,
	});
}
