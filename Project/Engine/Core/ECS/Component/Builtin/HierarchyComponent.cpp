#include "HierarchyComponent.h"

//============================================================================
//	HierarchyComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, HierarchyComponent& component) {

	std::string parentLocalFileID = in.value("parentLocalFileID", "");
	component.parentLocalFileID = parentLocalFileID.empty() ? UUID{} : FromString16Hex(parentLocalFileID);

	// ランタイム実行用のエンティティはシリアライズされないため、初期化しておく
	component.parent = Entity::Null();
	component.firstChild = Entity::Null();
	component.nextSibling = Entity::Null();
	component.prevSibling = Entity::Null();
}

void Engine::to_json(nlohmann::json& out, const HierarchyComponent& component) {

	out["parentLocalFileID"] = component.parentLocalFileID ? ToString(component.parentLocalFileID) : "";
}