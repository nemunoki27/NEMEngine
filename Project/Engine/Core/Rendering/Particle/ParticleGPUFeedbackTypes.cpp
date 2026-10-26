#include "ParticleGPUFeedbackTypes.h"

//============================================================================
//	include
//============================================================================
// c++
#include <unordered_map>
#include <utility>

bool Engine::HasParticleEventCycle(std::span<const Entity> sources, std::span<const std::vector<Entity>> targets) {

	if (sources.size() != targets.size()) return true;
	std::unordered_map<uint64_t, size_t> lookup;
	for (size_t index = 0; index < sources.size(); ++index) {
		lookup.emplace((uint64_t(sources[index].generation) << 32) | sources[index].index, index);
	}
	std::vector<uint8_t> marks(sources.size());
	std::vector<std::pair<size_t, size_t>> pending;
	for (size_t index = 0; index < sources.size(); ++index) {
		if (marks[index] == 2) continue;
		pending.emplace_back(index, 0);
		marks[index] = 1;
		while (!pending.empty()) {
			auto& [current, edge] = pending.back();
			if (edge == targets[current].size()) {
				marks[current] = 2;
				pending.pop_back();
				continue;
			}
			const Entity target = targets[current][edge++];
			const auto found = lookup.find((uint64_t(target.generation) << 32) | target.index);
			if (found == lookup.end() || marks[found->second] == 2) continue;
			if (marks[found->second] == 1) return true;
			pending.emplace_back(found->second, 0);
			marks[found->second] = 1;
		}
	}
	return false;
}
