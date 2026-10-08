#include "ECSWorld.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>

using namespace Engine;

//============================================================================
//	ECSWorld scopeMethods
//============================================================================

ECSWorld::StructuralScope::StructuralScope(ECSWorld& world) :
	world_(world), lifetime_(world.GetLifetime()), storageState_(world.storageState_) {

	if (world_.queryDepth_ != 0 || world_.structuralChange_) {
		throw std::logic_error("走査中または構造変更中の追加と削除はCommandへ予約してください");
	}
	world_.structuralChange_ = true;
}

ECSWorld::StructuralScope::~StructuralScope() {

	if (lifetime_->IsAlive()) {
		world_.structuralChange_ = false;
	}
}

ECSWorld::QueryScope::QueryScope(const ECSWorld& world) :
	world_(world), lifetime_(world.GetLifetime()), storageState_(world.storageState_) {

	if (world_.structuralChange_) {
		throw std::logic_error("構造変更途中のChunkを走査できません");
	}
	++world_.queryDepth_;
}

ECSWorld::QueryScope::~QueryScope() {

	if (lifetime_->IsAlive()) {
		--world_.queryDepth_;
	}
}
