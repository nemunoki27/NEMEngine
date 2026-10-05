#include "QuaternionAxisKeyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

namespace Engine::QuaternionAxisKeyUtility {

	void SortKeys(CurveChannel& channel, std::vector<CurveQuaternionAxisKey>& axisKeys) {

		// 不足した軸設定を補い、キーとの対応を保って並べる
		axisKeys.resize(channel.keys.size(), MakeDefault());
		std::vector<std::pair<CurveKey, CurveQuaternionAxisKey>> pairs;
		pairs.reserve(channel.keys.size());
		for (size_t i = 0; i < channel.keys.size(); ++i) {
			pairs.emplace_back(channel.keys[i], axisKeys[i]);
		}
		std::stable_sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.first.time < rhs.first.time;
		});
		for (size_t i = 0; i < pairs.size(); ++i) {
			channel.keys[i] = std::move(pairs[i].first);
			axisKeys[i] = std::move(pairs[i].second);
		}
	}

	CurveQuaternionAxisKey MakeDefault() {

		CurveQuaternionAxisKey axisKey{};
		axisKey.axes = { Axis::X };
		axisKey.customAxis = Vector3(1.0f, 0.0f, 0.0f);
		return axisKey;
	}

	Vector3 GetAxisDirection(const CurveQuaternionAxisKey& axisKey) {

		// 軸が無効な場合はX軸に倒して、Quaternion生成時のNaNを避ける
		Vector3 axis = axisKey.useCustomAxis ? axisKey.customAxis : GetDirection(axisKey.axes);
		if (axis.Length() <= 0.001f) {
			axis = Vector3(1.0f, 0.0f, 0.0f);
		}
		return axis.Normalize();
	}

	CurveQuaternionAxisKey Sanitize(const CurveQuaternionAxisKey& key) {

		CurveQuaternionAxisKey sanitized = key;
		if (sanitized.axes.empty()) {
			sanitized.axes = { Axis::X };
		}
		if (sanitized.useCustomAxis) {
			if (sanitized.customAxis.Length() <= 0.001f) {
				sanitized.customAxis = Vector3(1.0f, 0.0f, 0.0f);
			} else {
				sanitized.customAxis = sanitized.customAxis.Normalize();
			}
		}
		return sanitized;
	}

} // Engine::QuaternionAxisKeyUtility
