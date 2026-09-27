#include "MonoBehavior.h"

//============================================================================
//	MonoBehavior classMethods
//============================================================================
bool Engine::MonoBehavior::CaptureSavedFields([[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] nlohmann::json& fields) {

	// Native Behaviorは既存の保存値を維持する
	return true;
}
