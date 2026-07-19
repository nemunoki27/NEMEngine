#include "GameViewCameraSnapshot.h"

// c++
#include <cmath>

//============================================================================
//	GameViewCameraSnapshot classMethods
//============================================================================

Engine::GameViewCameraSnapshot::Snapshot Engine::GameViewCameraSnapshot::snapshot_{};

bool Engine::GameViewCameraSnapshot::TryWorldToScreenPoint(
	const Vector3& worldPosition, Vector3& outScreenPosition) {

	const Snapshot& camera = Get();
	if (!camera.valid || camera.width <= 0.0f || camera.height <= 0.0f) {
		return false;
	}

	const Matrix4x4& matrix = camera.viewProjection;
	const float clipX = worldPosition.x * matrix.m[0][0] +
		worldPosition.y * matrix.m[1][0] +
		worldPosition.z * matrix.m[2][0] + matrix.m[3][0];
	const float clipY = worldPosition.x * matrix.m[0][1] +
		worldPosition.y * matrix.m[1][1] +
		worldPosition.z * matrix.m[2][1] + matrix.m[3][1];
	const float clipW = worldPosition.x * matrix.m[0][3] +
		worldPosition.y * matrix.m[1][3] +
		worldPosition.z * matrix.m[2][3] + matrix.m[3][3];
	if (!std::isfinite(clipX) || !std::isfinite(clipY) ||
		!std::isfinite(clipW) || std::abs(clipW) <= 0.000001f) {
		return false;
	}

	const float ndcX = clipX / clipW;
	const float ndcY = clipY / clipW;
	outScreenPosition = Vector3(
		(ndcX * 0.5f + 0.5f) * camera.width,
		(0.5f - ndcY * 0.5f) * camera.height,
		clipW);
	return std::isfinite(outScreenPosition.x) &&
		std::isfinite(outScreenPosition.y);
}
