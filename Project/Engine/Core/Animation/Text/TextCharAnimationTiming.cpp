#include "TextCharAnimationTiming.h"

#include <algorithm>
#include <numeric>
#include <random>

namespace {

	using namespace Engine;

	void SetStartTimes(const TextCharAnimationSettings& settings, std::span<float> starts,
		float glyphDuration, size_t first, float& duration) {

		const size_t count = starts.size();
		const bool paired = settings.order == TextCharAnimationOrder::EndsToCenter ||
			settings.order == TextCharAnimationOrder::CenterToEnds;
		const size_t groups = settings.order == TextCharAnimationOrder::Simultaneous ? 1 : paired ? (count + 1) / 2 : count;
		const float spread = groups <= 1 ? 0.0f : settings.timing == TextCharAnimationTiming::Interval ?
			settings.interval * static_cast<float>(groups - 1) : (std::max)(0.0f, settings.totalDuration - glyphDuration);
		duration = (std::max)(duration, spread + glyphDuration);

		std::vector<size_t> ranks;
		if (settings.order == TextCharAnimationOrder::Random) {
			ranks.resize(count);
			std::iota(ranks.begin(), ranks.end(), size_t{0});
			std::mt19937 rng(static_cast<uint32_t>(settings.seed) + static_cast<uint32_t>(first));
			std::shuffle(ranks.begin(), ranks.end(), rng);
		}
		for (size_t i = 0; i < count; ++i) {
			size_t rank = i;
			switch (settings.order) {
			case TextCharAnimationOrder::Simultaneous: rank = 0; break;
			case TextCharAnimationOrder::RightToLeft: rank = count - 1 - i; break;
			case TextCharAnimationOrder::EndsToCenter: rank = (std::min)(i, count - 1 - i); break;
			case TextCharAnimationOrder::CenterToEnds: rank = groups - 1 - (std::min)(i, count - 1 - i); break;
			case TextCharAnimationOrder::Random: rank = ranks[i]; break;
			default: break;
			}
			if (rank == 0) starts[i] = 0;
			else if (rank + 1 == groups) starts[i] = spread;
			else starts[i] = spread * EasedValue(settings.easing, static_cast<float>(rank) / static_cast<float>(groups - 1));
		}
	}
}

void Engine::UpdateTextCharSchedule(TextCharAnimationSchedule& s, const TextCharAnimationSettings& settings,
	std::span<const TextLayoutGlyph> glyphs, uint64_t layoutRevision, float glyphDuration) {

	if (s.layoutRevision == layoutRevision && s.starts.size() == glyphs.size() && s.order == settings.order &&
		s.timing == settings.timing && s.lines == settings.lines && s.easing == settings.easing &&
		s.interval == settings.interval && s.totalDuration == settings.totalDuration &&
		s.seed == settings.seed && s.glyphDuration == glyphDuration) return;
	s.layoutRevision = layoutRevision;
	s.order = settings.order;
	s.timing = settings.timing;
	s.lines = settings.lines;
	s.easing = settings.easing;
	s.interval = settings.interval;
	s.totalDuration = settings.totalDuration;
	s.seed = settings.seed;
	s.glyphDuration = glyphDuration;
	s.duration = 0;
	s.starts.resize(glyphs.size());
	for (size_t first = 0; first < glyphs.size();) {
		size_t end = glyphs.size();
		if (s.lines == TextCharAnimationLines::PerLine) {
			end = first + 1;
			while (end < glyphs.size() && glyphs[end].lineIndex == glyphs[first].lineIndex) ++end;
		}
		SetStartTimes(settings, std::span(s.starts).subspan(first, end - first), glyphDuration, first, s.duration);
		first = end;
	}
}
