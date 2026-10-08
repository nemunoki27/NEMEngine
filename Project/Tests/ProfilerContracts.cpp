#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfileHistory.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfileSummary.h>

bool NEMTests::TestProfilerContracts() {

	Engine::FrameProfileHistory history;
	history.BeginFrame(true);
	history.Add(2.0f);
	history.Add(3.0f);
	if (history.GetAverage() != 5.0f) return false;
	history.BeginFrame(false);
	history.Add(100.0f);
	if (history.GetAverage() != 5.0f) return false;
	history.BeginFrame(false);
	if (history.GetAverage() != 52.5f) return false;
	for (int i = 0; i < 8; ++i) {
		history.Add(4.0f);
		history.BeginFrame(false);
	}
	if (history.GetAverage() != 4.0f) return false;

	std::vector<Engine::ScriptProfileRow> rows(2);
	rows[0].name = "first";
	rows[0].history[0] = { 4.0, 1.0, 2 };
	rows[0].history[1] = { 2.0, 0.5, 1 };
	rows[1].name = "second";
	rows[1].history[1] = { 5.0, 3.0, 1 };
	const auto summary = Engine::SummarizeScriptProfile(rows, 1, 2, 1);
	return summary.size() == 2 && summary[0].name == "first" && summary[0].average == 3.0 &&
		summary[0].latest == 2.0 && summary[0].self == 0.5 && summary[0].averageCalls == 1.5 &&
		Engine::SummarizeScriptProfile(rows, 1, 2, 0)[0].name == "second";
}
