#include "Axis.h"

using namespace Engine;

//============================================================================
//	Axis classMethods
//============================================================================

Vector3 Engine::GetDirection(const std::vector<Axis>& axes) {

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
	return direction.Normalize();
}