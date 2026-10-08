#include "Axis.h"

using namespace Engine;

//============================================================================
//	Axis classMethods
//============================================================================
Vector3 Engine::GetDirection(const std::vector<Axis>& axes) {

	// 指定された軸方向を加算
	Vector3 direction{};
	for (const auto& axis : axes) {
		switch (axis) {
		case Axis::X: {

			direction += Vector3(1.0f, 0.0f, 0.0f);
			break;
		}
		case Axis::Y: {

			direction += Vector3(0.0f, 1.0f, 0.0f);
			break;
		}
		case Axis::Z: {

			direction += Vector3(0.0f, 0.0f, 1.0f);
			break;
		}
		}
	}
	// 合成した方向を正規化
	return direction.Normalize();
}