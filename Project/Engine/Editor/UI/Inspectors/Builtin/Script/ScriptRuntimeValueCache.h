#pragma once

#include <Engine/Core/World/Behavior/BehaviorHandle.h>

#include <chrono>
#include <unordered_map>
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ScriptRuntimeValueCache class
	//	Inspectorで表示する実行中Fieldの読戻し間隔と値を所有する
	//============================================================================
	class ScriptRuntimeValueCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void BeginFrame(bool playing);
		nlohmann::json& GetState(BehaviorHandle handle);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static constexpr size_t kMaximumEntries = 64;
		std::chrono::steady_clock::time_point now_;
		std::unordered_map<uint64_t, std::pair<std::chrono::steady_clock::time_point, nlohmann::json>> values_;
	};
}
