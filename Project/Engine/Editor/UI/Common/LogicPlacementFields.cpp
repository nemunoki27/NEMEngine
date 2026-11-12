#include "LogicPlacementFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

#include <algorithm>
#include <array>

using namespace Engine;

namespace {

	// 共通のプロパティ行で選択肢を表示する
	template<typename T, size_t N>
	bool DrawChoice(const char* label, T& value, const std::array<std::string, N>& names, size_t count = N) {

		const auto index = static_cast<size_t>(value);
		std::string selected = names[index < count ? index : 0];
		const auto result = MyGUI::StringCombo(label, selected, std::span(names).first(count));
		if (result.valueChanged) value = static_cast<T>(std::find(names.begin(), names.end(), selected) - names.begin());
		return result.valueChanged;
	}
}

bool Engine::LogicPlacementFields::DrawShape(LogicPlacementSettings& settings, bool spline) {

	static const std::array<std::string, 4> shapes{"円状", "N角形", "直線", "格子"};
	static const std::array<std::string, 3> planes{"XY", "XZ", "YZ"};
	static const std::array<std::string, 2> distributions{"均等", "ランダム"};
	bool changed = DrawChoice("形状", settings.shape, shapes, spline ? 3 : 4);
	if (!settings.is2D) changed |= DrawChoice("配置平面", settings.plane, planes);
	const auto count = [&](const char* label, int32_t& value, int32_t minimum = 1) {
		return MyGUI::DragInt(label, value, {.minValue = minimum, .maxValue = spline ? 4096 : 10000}).valueChanged;
	};
	const auto positive = [&](const char* label, float& value) {
		return MyGUI::DragFloat(label, value, {.dragSpeed = 0.1f, .minValue = 0.001f, .maxValue = 1000000.0f}).valueChanged;
	};
	const bool radial = settings.shape == LogicPlacementShape::Circle || settings.shape == LogicPlacementShape::Polygon;
	if (radial) {
		changed |= positive("半径", settings.radius);
		if (settings.shape == LogicPlacementShape::Circle) changed |= count("外周の配置数", settings.count, 3);
		else {
			changed |= count("頂点数", settings.vertices, 3);
			changed |= count("辺の分割数", settings.subdivisions);
		}
		changed |= MyGUI::DragFloat("開始角度", settings.startAngle, {.dragSpeed = 1.0f, .minValue = -360, .maxValue = 360}).valueChanged;
		if (!spline) {
			changed |= MyGUI::Checkbox("中を埋め尽くす", settings.fill);
			if (settings.fill) {
				ImGui::Indent();
				changed |= DrawChoice("内部分布", settings.distribution, distributions);
				if (settings.distribution == LogicPlacementDistribution::Uniform) {
					changed |= count("内側の段数", settings.interiorLayers, 0);
				} else {
					changed |= count("内部の配置数", settings.interiorCount, 0);
					changed |= MyGUI::DragInt("シード", settings.seed, {.minValue = 0}).valueChanged;
				}
				ImGui::Unindent();
			}
		}
	} else if (settings.shape == LogicPlacementShape::Line) {
		changed |= positive("長さ", settings.length);
		changed |= count("配置数", settings.count, 2);
	} else {
		changed |= count("横方向の個数", settings.columns);
		changed |= count("縦方向の個数", settings.rows);
		if (!settings.is2D) changed |= count("高さ方向の個数", settings.layers);
		changed |= positive("横方向の間隔", settings.spacing.x);
		changed |= positive("縦方向の間隔", settings.spacing.y);
		if (!settings.is2D) changed |= positive("高さ方向の間隔", settings.spacing.z);
	}
	return changed;
}

bool Engine::LogicPlacementFields::DrawOrientation(LogicPlacementRequest& request) {

	static const std::array<std::string, 3> modes{"元の向き", "進行方向", "中心方向"};
	static const std::array<std::string, 6> axes{"+X", "+Y", "+Z", "-X", "-Y", "-Z"};
	static const std::array<std::string, 4> axes2D{"+X", "+Y", "-X", "-Y"};
	bool changed = DrawChoice("向き", request.orientation, modes);
	if (request.orientation == LogicPlacementOrientation::Original) return changed;
	if (request.settings.is2D) {
		int32_t selected = request.forward == LogicPlacementForwardAxis::PositiveY ? 1 :
			request.forward == LogicPlacementForwardAxis::NegativeX ? 2 : request.forward == LogicPlacementForwardAxis::NegativeY ? 3 : 0;
		if (DrawChoice("前方軸", selected, axes2D)) {
			const std::array values{LogicPlacementForwardAxis::PositiveX, LogicPlacementForwardAxis::PositiveY,
				LogicPlacementForwardAxis::NegativeX, LogicPlacementForwardAxis::NegativeY};
			request.forward = values[selected];
			changed = true;
		}
	} else changed |= DrawChoice("前方軸", request.forward, axes);
	if (request.settings.is2D) changed |= MyGUI::DragFloat("回転補正", request.rotationOffset.z,
		{.dragSpeed = 1, .minValue = -360, .maxValue = 360}).valueChanged;
	else changed |= MyGUI::DragVector3("回転補正", request.rotationOffset,
		{.dragSpeed = 1, .minValue = -360, .maxValue = 360}).valueChanged;
	return changed;
}
