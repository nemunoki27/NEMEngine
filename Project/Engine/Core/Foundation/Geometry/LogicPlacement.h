#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	enum class LogicPlacementShape : uint8_t { Circle, Polygon, Line, Grid };
	enum class LogicPlacementPlane : uint8_t { XY, XZ, YZ };
	enum class LogicPlacementDistribution : uint8_t { Uniform, Random };

	// 親のローカル空間で配置点を作る設定
	struct LogicPlacementSettings {

		LogicPlacementShape shape = LogicPlacementShape::Circle;
		LogicPlacementPlane plane = LogicPlacementPlane::XZ;
		bool is2D = false;
		float radius = 1.0f;
		float startAngle = 0.0f;
		int32_t count = 8;
		int32_t vertices = 3;
		int32_t subdivisions = 1;
		float length = 1.0f;
		bool fill = false;
		int32_t interiorCount = 16;
		int32_t interiorLayers = 2;
		LogicPlacementDistribution distribution = LogicPlacementDistribution::Uniform;
		int32_t seed = 0;
		int32_t columns = 2;
		int32_t rows = 2;
		int32_t layers = 2;
		Vector3 spacing{1, 1, 1};
	};

	struct LogicPlacementPoint {

		Vector3 position{};
		Vector3 direction{};
	};

	struct LogicPlacementLine {

		Vector3 from{};
		Vector3 to{};
	};

	struct LogicPlacementResult {

		std::vector<LogicPlacementPoint> points;
		std::vector<LogicPlacementLine> lines;
		Vector3 normal{0, 1, 0};
		uint32_t boundaryCount = 0;
		std::string diagnostic;

		bool IsValid() const { return diagnostic.empty() && !points.empty(); }
	};

	// シーンを変更せず同じ設定から同じ配置を生成する
	LogicPlacementResult BuildLogicPlacement(const LogicPlacementSettings& settings, uint32_t maxPoints = 10000);

	// json変換
	void to_json(nlohmann::json& out, const LogicPlacementSettings& settings);
	void from_json(const nlohmann::json& in, LogicPlacementSettings& settings);
}
