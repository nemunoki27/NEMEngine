#include "AnimationEventCollection.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

void Engine::AnimationEventCollection::CollectClipEvents(const AnimationClipAsset& clip,
	std::span<const AnimationPlaybackTime::Interval> intervals, std::vector<AnimationEvent>& firedOut) {

	// 時計が実際に通過した区間だけを配送する
	for (const auto& interval : intervals) {

		const bool forward = interval.to >= interval.from;
		const size_t first = firedOut.size();
		for (const AnimationEvent& event : clip.events) {

			const bool passed = forward ? event.time > interval.from && event.time <= interval.to :
				event.time < interval.from && event.time >= interval.to;
			if (passed || (interval.includeStart && event.time == interval.from)) firedOut.push_back(event);
		}
		// 保存順に依存せず、逆再生も通過順で通知する
		std::stable_sort(firedOut.begin() + first, firedOut.end(), [&](const auto& left, const auto& right) {
			return forward ? left.time < right.time : left.time > right.time;
		});
	}
}
