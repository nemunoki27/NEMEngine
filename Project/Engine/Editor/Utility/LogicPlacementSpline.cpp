#include "LogicPlacementSpline.h"

#include <cmath>
#include <numbers>

using namespace Engine;

bool Engine::ApplyLogicPlacementSpline(const LogicPlacementSettings& settings, const LogicPlacementResult& layout, SplineData& data) {

	if (!layout.IsValid() || settings.shape == LogicPlacementShape::Grid || settings.fill ||
		layout.points.size() > 4096 || (settings.shape == LogicPlacementShape::Circle && layout.points.size() < 3)) return false;
	SplineData candidate = data;
	candidate.points.clear();
	candidate.closed = settings.shape != LogicPlacementShape::Line;
	candidate.is2D = settings.is2D;
	for (const auto& point : layout.points) candidate.points.push_back({point.position, {}, {}});
	for (size_t i = 0; i < candidate.points.size(); ++i) {
		auto& point = candidate.points[i];
		if (settings.shape == LogicPlacementShape::Circle) {
			const float length = static_cast<float>(4.0 / 3.0 * std::tan(std::numbers::pi / (2 * candidate.points.size())) * settings.radius);
			point.tangentOut = layout.points[i].direction * length;
			point.tangentIn = -point.tangentOut;
		} else {
			if (i > 0 || candidate.closed)
				point.tangentIn = (candidate.points[(i + candidate.points.size() - 1) % candidate.points.size()].position - point.position) / 3;
			if (i + 1 < candidate.points.size() || candidate.closed)
				point.tangentOut = (candidate.points[(i + 1) % candidate.points.size()].position - point.position) / 3;
		}
	}
	data = std::move(candidate);
	return true;
}
