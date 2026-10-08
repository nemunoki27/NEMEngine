#include "ScriptProfileSummary.h"

// c++
#include <algorithm>

std::vector<Engine::ScriptProfileSummary> Engine::SummarizeScriptProfile(
	const std::vector<ScriptProfileRow>& source, uint32_t lastFrame, uint32_t frameCount, int sort) {

	std::vector<ScriptProfileSummary> rows;
	for (size_t i = 0; i < source.size(); ++i) {
		const auto& row = source[i];
		ScriptProfileSummary view{};
		view.owner = row.owner;
		view.name = row.name;
		view.id = static_cast<int32_t>(i);
		view.parent = row.parent;
		view.detail = row.detail;
		view.grouped = row.grouped;
		const auto& latest = row.history[lastFrame];
		view.latest = latest.inclusiveMs;
		view.self = latest.selfMs;
		view.calls = latest.calls;
		for (const auto& frame : row.history) {
			view.average += frame.inclusiveMs;
			view.maximum = std::max(view.maximum, frame.inclusiveMs);
			view.averageCalls += frame.calls;
		}
		const double count = std::max(1u, frameCount);
		view.average /= count;
		view.averageCalls /= count;
		rows.push_back(std::move(view));
	}
	std::stable_sort(rows.begin(), rows.end(), [sort](const ScriptProfileSummary& a, const ScriptProfileSummary& b) {
		if (sort == 0) { return a.latest > b.latest; }
		if (sort == 2) { return a.maximum > b.maximum; }
		if (sort == 3) { return a.calls > b.calls; }
		return a.average > b.average;
	});
	return rows;
}
