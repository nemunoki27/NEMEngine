#pragma once

//============================================================================
//	include
//============================================================================
#include "ScriptProfiler.h"

namespace Engine {

	// 表示側が所有する区間ごとの集計結果
	struct ScriptProfileSummary {

		ScriptProfileOwner owner;
		std::string name;
		int32_t id = -1;
		int32_t parent = -1;
		bool detail = false;
		bool grouped = false;
		double latest = 0;
		double average = 0;
		double maximum = 0;
		double self = 0;
		uint32_t calls = 0;
		double averageCalls = 0;
	};
	// 確定履歴を表示順の集計値へ変換する
	std::vector<ScriptProfileSummary> SummarizeScriptProfile(const std::vector<ScriptProfileRow>& source,
		uint32_t lastFrame, uint32_t frameCount, int sort);
}
