#include "QuaternionAxisKeyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine::QuaternionAxisKeyUtility {

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
