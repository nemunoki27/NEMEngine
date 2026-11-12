#include "LogicPlacement.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

using namespace Engine;

namespace {

	// 選択した平面へ座標を写す
	Vector3 PlanePoint(const LogicPlacementSettings& settings, double u, double v, double height = 0) {

		const auto plane = settings.is2D ? LogicPlacementPlane::XY : settings.plane;
		if (plane == LogicPlacementPlane::XY) return {static_cast<float>(u), static_cast<float>(v), static_cast<float>(height)};
		if (plane == LogicPlacementPlane::YZ) return {static_cast<float>(height), static_cast<float>(u), static_cast<float>(v)};
		return {static_cast<float>(u), static_cast<float>(height), static_cast<float>(v)};
	}

	// 平面上の角度と半径から座標を作る
	Vector3 RadialPoint(const LogicPlacementSettings& settings, double angle, double radius) {

		return PlanePoint(settings, std::cos(angle) * radius, std::sin(angle) * radius);
	}

	// 外周の点間隔に合わせて内側の分割数を求める
	int32_t LayerDivisions(const LogicPlacementSettings& settings, double fraction) {

		const bool circle = settings.shape == LogicPlacementShape::Circle;
		const int32_t divisions = static_cast<int32_t>(std::round((circle ? settings.count : settings.subdivisions) * fraction));
		return (std::max)(circle ? 3 : 1, divisions);
	}

	// 相似形の一段分の配置点と補助線を追加する
	void AppendRadialLayer(const LogicPlacementSettings& settings, double fraction, int32_t divisions, LogicPlacementResult& result) {

		const double turn = std::numbers::pi * 2;
		const double start = settings.startAngle * std::numbers::pi / 180;
		const double radius = settings.radius * fraction;
		if (settings.shape == LogicPlacementShape::Circle) {
			for (int32_t i = 0; i < divisions; ++i) {
				const double angle = start + turn * i / divisions;
				result.points.push_back({RadialPoint(settings, angle, radius), RadialPoint(settings, angle + std::numbers::pi / 2, 1)});
			}
			const int32_t segments = (std::max)(64, divisions);
			for (int32_t i = 0; i < segments; ++i) result.lines.push_back({
				RadialPoint(settings, start + turn * i / segments, radius),
				RadialPoint(settings, start + turn * (i + 1) / segments, radius)});
		} else {
			for (int32_t edge = 0; edge < settings.vertices; ++edge) {
				const auto a = RadialPoint(settings, start + turn * edge / settings.vertices, radius);
				const auto b = RadialPoint(settings, start + turn * (edge + 1) / settings.vertices, radius);
				for (int32_t i = 0; i < divisions; ++i)
					result.points.push_back({Vector3::Lerp(a, b, static_cast<float>(i) / divisions), (b - a).Normalize()});
				result.lines.push_back({a, b});
			}
		}
	}
}

LogicPlacementResult Engine::BuildLogicPlacement(const LogicPlacementSettings& settings, uint32_t maxPoints) {

	LogicPlacementResult result;
	const auto fail = [&](const char* message) {
		result.points.clear();
		result.lines.clear();
		result.diagnostic = message;
		return result;
	};
	if (settings.shape > LogicPlacementShape::Grid || settings.plane > LogicPlacementPlane::YZ ||
		settings.distribution > LogicPlacementDistribution::Random) return fail("配置設定が無効です");
	const bool radial = settings.shape == LogicPlacementShape::Circle || settings.shape == LogicPlacementShape::Polygon;
	if (radial && (!std::isfinite(settings.radius) || settings.radius <= 0 || !std::isfinite(settings.startAngle)))
		return fail("半径は0より大きい有限の値を設定してください");
	if (settings.shape == LogicPlacementShape::Polygon && (settings.vertices < 3 || settings.subdivisions < 1))
		return fail("頂点数は3以上、辺の分割数は1以上にしてください");
	if ((settings.shape == LogicPlacementShape::Circle || settings.shape == LogicPlacementShape::Line) && settings.count < 2)
		return fail("配置数は2以上にしてください");
	if (settings.shape == LogicPlacementShape::Line && (!std::isfinite(settings.length) || settings.length <= 0))
		return fail("長さは0より大きい有限の値を設定してください");
	if (settings.shape == LogicPlacementShape::Grid && (settings.columns < 1 || settings.rows < 1 ||
		(!settings.is2D && settings.layers < 1) || !std::isfinite(settings.spacing.x) || !std::isfinite(settings.spacing.y) ||
		!std::isfinite(settings.spacing.z) || settings.spacing.x <= 0 || settings.spacing.y <= 0 || settings.spacing.z <= 0))
		return fail("格子の個数と間隔は0より大きい値にしてください");
	const bool uniformFill = radial && settings.fill && settings.distribution == LogicPlacementDistribution::Uniform;
	if (uniformFill && settings.interiorLayers < 0) return fail("内側の段数は0以上にしてください");
	if (radial && settings.fill && !uniformFill && settings.interiorCount < 0) return fail("内部の配置数は0以上にしてください");
	uint64_t count = static_cast<uint64_t>(settings.count);
	if (settings.shape == LogicPlacementShape::Polygon) count = static_cast<uint64_t>(settings.vertices) * settings.subdivisions;
	if (settings.shape == LogicPlacementShape::Grid) {
		count = static_cast<uint64_t>(settings.columns) * settings.rows;
		if (count > maxPoints) return fail("配置数が上限を超えています");
		count *= settings.is2D ? 1 : settings.layers;
	}
	if (uniformFill) {
		if (count > maxPoints || static_cast<uint64_t>(settings.interiorLayers) > maxPoints)
			return fail("配置数が上限を超えています");
		++count;
		for (int32_t layer = 1; layer <= settings.interiorLayers; ++layer) {
			const double fraction = 1.0 - static_cast<double>(layer) / (static_cast<double>(settings.interiorLayers) + 1);
			const auto divisions = static_cast<uint64_t>(LayerDivisions(settings, fraction));
			count += settings.shape == LogicPlacementShape::Circle ? divisions : divisions * settings.vertices;
			if (count > maxPoints) return fail("配置数が上限を超えています");
		}
	} else if (radial && settings.fill) count += settings.interiorCount;
	if (count == 0 || count > maxPoints) return fail("配置数が上限を超えています");
	result.points.reserve(static_cast<size_t>(count));
	result.normal = PlanePoint(settings, 0, 0, 1);
	const double turn = std::numbers::pi * 2;
	const double start = settings.startAngle * std::numbers::pi / 180;
	const auto addRadial = [&](double angle, double radius) {
		result.points.push_back({RadialPoint(settings, angle, radius), RadialPoint(settings, angle + std::numbers::pi / 2, 1)});
	};
	if (radial) {
		AppendRadialLayer(settings, 1, settings.shape == LogicPlacementShape::Circle ? settings.count : settings.subdivisions, result);
	} else if (settings.shape == LogicPlacementShape::Line) {
		for (int32_t i = 0; i < settings.count; ++i) result.points.push_back({
			PlanePoint(settings, settings.length * (static_cast<double>(i) / (settings.count - 1) - 0.5), 0), PlanePoint(settings, 1, 0)});
		result.lines.push_back({result.points.front().position, result.points.back().position});
	} else {
		const int32_t layers = settings.is2D ? 1 : settings.layers;
		for (int32_t layer = 0; layer < layers; ++layer) for (int32_t row = 0; row < settings.rows; ++row)
			for (int32_t column = 0; column < settings.columns; ++column) {
				const auto point = PlanePoint(settings, (column - (settings.columns - 1) * 0.5) * settings.spacing.x,
					(row - (settings.rows - 1) * 0.5) * settings.spacing.y, (layer - (layers - 1) * 0.5) * settings.spacing.z);
				const size_t index = result.points.size();
				result.points.push_back({point, PlanePoint(settings, 1, 0)});
				if (column > 0) result.lines.push_back({result.points[index - 1].position, point});
				if (row > 0) result.lines.push_back({result.points[index - settings.columns].position, point});
				if (layer > 0) result.lines.push_back({result.points[index - static_cast<size_t>(settings.columns) * settings.rows].position, point});
			}
	}
	result.boundaryCount = static_cast<uint32_t>(result.points.size());
	if (uniformFill) {
		for (int32_t layer = 1; layer <= settings.interiorLayers; ++layer) {
			const double fraction = 1.0 - static_cast<double>(layer) / (static_cast<double>(settings.interiorLayers) + 1);
			AppendRadialLayer(settings, fraction, LayerDivisions(settings, fraction), result);
		}
		result.points.push_back({{}, {}});
	} else if (radial && settings.fill) {
		std::mt19937 random(static_cast<uint32_t>(settings.seed));
		const auto next = [&]() { return (static_cast<double>(random()) + 0.5) / 4294967296.0; };
		for (int32_t i = 0; i < settings.interiorCount; ++i) {
			double u = next();
			const double v = next();
			if (settings.shape == LogicPlacementShape::Circle) addRadial(start + turn * v, settings.radius * std::sqrt(u));
			else {
				u *= settings.vertices;
				const auto edge = static_cast<int32_t>(u);
				const auto a = RadialPoint(settings, start + turn * edge / settings.vertices, settings.radius);
				const auto b = RadialPoint(settings, start + turn * (edge + 1) / settings.vertices, settings.radius);
				const auto point = Vector3::Lerp(a, b, static_cast<float>(v)) * static_cast<float>(std::sqrt(u - edge));
				result.points.push_back({point, (b - a).Normalize()});
			}
		}
	}
	return result;
}

void Engine::to_json(nlohmann::json& out, const LogicPlacementSettings& settings) {

	out = {{"shape", settings.shape}, {"plane", settings.plane}, {"is2D", settings.is2D},
		{"radius", settings.radius}, {"startAngle", settings.startAngle}, {"count", settings.count},
		{"vertices", settings.vertices}, {"subdivisions", settings.subdivisions}, {"length", settings.length},
		{"fill", settings.fill}, {"interiorCount", settings.interiorCount}, {"interiorLayers", settings.interiorLayers},
		{"distribution", settings.distribution},
		{"seed", settings.seed}, {"columns", settings.columns}, {"rows", settings.rows}, {"layers", settings.layers},
		{"spacing", settings.spacing.ToJson()}};
}

void Engine::from_json(const nlohmann::json& in, LogicPlacementSettings& settings) {

	LogicPlacementSettings value;
	if (!in.is_object()) { settings = value; return; }
	value.shape = in.value("shape", value.shape);
	value.plane = in.value("plane", value.plane);
	value.is2D = in.value("is2D", value.is2D);
	value.radius = in.value("radius", value.radius);
	value.startAngle = in.value("startAngle", value.startAngle);
	value.count = in.value("count", value.count);
	value.vertices = in.value("vertices", value.vertices);
	value.subdivisions = in.value("subdivisions", value.subdivisions);
	value.length = in.value("length", value.length);
	value.fill = in.value("fill", value.fill);
	value.interiorCount = in.value("interiorCount", value.interiorCount);
	value.interiorLayers = in.value("interiorLayers", value.interiorLayers);
	value.distribution = in.value("distribution", value.distribution);
	value.seed = in.value("seed", value.seed);
	value.columns = in.value("columns", value.columns);
	value.rows = in.value("rows", value.rows);
	value.layers = in.value("layers", value.layers);
	value.spacing = Vector3::FromJson(in.value("spacing", value.spacing.ToJson()));
	settings = value;
}
