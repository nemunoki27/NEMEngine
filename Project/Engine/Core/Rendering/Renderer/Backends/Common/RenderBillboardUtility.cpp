#include "RenderBillboardUtility.h"

//============================================================================
//	include
//============================================================================
namespace Engine::RenderBillboard {

	Matrix4x4 ResolveParentWorldMatrix(ECSWorld& world, const Entity& entity) {

		const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity);
		if (!hierarchy || !hierarchy->parent.IsValid() || !world.IsAlive(hierarchy->parent)) {
			return Matrix4x4::Identity();
		}

		const auto* parentTransform = world.TryGetComponent<TransformComponent>(hierarchy->parent);
		return parentTransform ? parentTransform->worldMatrix : Matrix4x4::Identity();
	}

	Quaternion QuaternionFromRotationMatrix(const Matrix4x4& matrix) {

		const float trace = matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2];

		Quaternion result{};

		if (trace > 0.0f) {
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			result.w = 0.25f * s;
			result.x = (matrix.m[1][2] - matrix.m[2][1]) / s;
			result.y = (matrix.m[2][0] - matrix.m[0][2]) / s;
			result.z = (matrix.m[0][1] - matrix.m[1][0]) / s;

		} else if (matrix.m[0][0] > matrix.m[1][1] && matrix.m[0][0] > matrix.m[2][2]) {
			const float s = std::sqrt(1.0f + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2]) * 2.0f;
			result.w = (matrix.m[1][2] - matrix.m[2][1]) / s;
			result.x = 0.25f * s;
			result.y = (matrix.m[0][1] + matrix.m[1][0]) / s;
			result.z = (matrix.m[0][2] + matrix.m[2][0]) / s;

		} else if (matrix.m[1][1] > matrix.m[2][2]) {
			const float s = std::sqrt(1.0f + matrix.m[1][1] - matrix.m[0][0] - matrix.m[2][2]) * 2.0f;
			result.w = (matrix.m[2][0] - matrix.m[0][2]) / s;
			result.x = (matrix.m[0][1] + matrix.m[1][0]) / s;
			result.y = 0.25f * s;
			result.z = (matrix.m[1][2] + matrix.m[2][1]) / s;

		} else {
			const float s = std::sqrt(1.0f + matrix.m[2][2] - matrix.m[0][0] - matrix.m[1][1]) * 2.0f;
			result.w = (matrix.m[0][1] - matrix.m[1][0]) / s;
			result.x = (matrix.m[0][2] + matrix.m[2][0]) / s;
			result.y = (matrix.m[1][2] + matrix.m[2][1]) / s;
			result.z = 0.25f * s;
		}

		return Quaternion::Normalize(result);
	}

	Quaternion MakeLookAtBillboardRotation(const Vector3& objectWorldPos, const Vector3& cameraWorldPos) {

		Vector3 forward = cameraWorldPos - objectWorldPos;
		forward = Vector3::NormalizeOr(forward, { 0.0f, 0.0f, 1.0f });

		Vector3 up = { 0.0f, 1.0f, 0.0f };
		if (std::fabs(Vector3::Dot(forward, up)) > 0.999f) {
			up = { 1.0f, 0.0f, 0.0f };
		}

		Vector3 right = Vector3::Normalize(Vector3::Cross(up, forward));
		up = Vector3::Cross(forward, right);

		Matrix4x4 billboardMatrix = Matrix4x4::Identity();
		billboardMatrix.m[0][0] = right.x;
		billboardMatrix.m[0][1] = right.y;
		billboardMatrix.m[0][2] = right.z;
		billboardMatrix.m[1][0] = up.x;
		billboardMatrix.m[1][1] = up.y;
		billboardMatrix.m[1][2] = up.z;
		billboardMatrix.m[2][0] = forward.x;
		billboardMatrix.m[2][1] = forward.y;
		billboardMatrix.m[2][2] = forward.z;

		return QuaternionFromRotationMatrix(billboardMatrix);
	}

	Quaternion MakeCameraBillboardRotation(const ResolvedCameraView& camera, const Vector3& objectWorldPos) {

		const Matrix4x4 cameraWorldMatrix = Matrix4x4::Inverse(camera.matrices.viewMatrix);
		const Vector3 cameraWorldPos = cameraWorldMatrix.GetTranslationValue();
		return MakeLookAtBillboardRotation(objectWorldPos, cameraWorldPos);
	}

	Quaternion ApplyAxisMask(const Quaternion& currentLocal, const Quaternion& desiredLocal, const BillboardComponent& billboard, const Vector3& localForward) {
		
		Vector3 currentEuler = Quaternion::ToEulerDegrees(currentLocal);
		
		// 全軸許可されている場合は素直にdesiredLocalのオイラーを連続化して使う
		if (HasBillboardAxis(billboard, Axis::X) && HasBillboardAxis(billboard, Axis::Y) && HasBillboardAxis(billboard, Axis::Z)) {
			Vector3 desiredEuler = Vector3::MakeContinuousDegrees(Quaternion::ToEulerDegrees(desiredLocal), currentEuler);
			return Quaternion::Normalize(Quaternion::FromEulerDegrees(desiredEuler));
		}

		Vector3 targetEuler = currentEuler;

		// フリップ防止のために方向ベクトルから直接Euler(Yaw/Pitch)を計算する
		float yaw = Math::RadToDeg(std::atan2(localForward.x, localForward.z));
		float xzLen = std::sqrt(localForward.x * localForward.x + localForward.z * localForward.z);
		float pitch = Math::RadToDeg(std::atan2(-localForward.y, xzLen));

		if (HasBillboardAxis(billboard, Axis::X)) {
			targetEuler.x = pitch;
		}
		if (HasBillboardAxis(billboard, Axis::Y)) {
			targetEuler.y = yaw;
		}
		if (HasBillboardAxis(billboard, Axis::Z)) {
			// ZだけはLookAtからは計算しにくいため、desiredLocalから持ってくる
			Vector3 desiredEuler = Vector3::MakeContinuousDegrees(Quaternion::ToEulerDegrees(desiredLocal), currentEuler);
			targetEuler.z = desiredEuler.z;
		}

		// 最短回転になるように連続化
		targetEuler = Vector3::MakeContinuousDegrees(targetEuler, currentEuler);

		return Quaternion::Normalize(Quaternion::FromEulerDegrees(targetEuler));
	}

	Quaternion ExtractRotationNoScale(const Matrix4x4& matrix) {

		Vector3 right = Vector3::NormalizeOr({ matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] }, { 1.0f, 0.0f, 0.0f });
		Vector3 up = Vector3::NormalizeOr({ matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] }, { 0.0f, 1.0f, 0.0f });
		Vector3 forward = Vector3::NormalizeOr({ matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] }, { 0.0f, 0.0f, 1.0f });

		Matrix4x4 rotationMatrix = Matrix4x4::Identity();
		rotationMatrix.m[0][0] = right.x; rotationMatrix.m[0][1] = right.y; rotationMatrix.m[0][2] = right.z;
		rotationMatrix.m[1][0] = up.x;    rotationMatrix.m[1][1] = up.y;    rotationMatrix.m[1][2] = up.z;
		rotationMatrix.m[2][0] = forward.x; rotationMatrix.m[2][1] = forward.y; rotationMatrix.m[2][2] = forward.z;

		return QuaternionFromRotationMatrix(rotationMatrix);
	}

	Matrix4x4 ResolveWorldMatrix(const RenderItem& item, const ResolvedRenderView& view) {

		if (!HasBillboard(item)) {
			return item.worldMatrix;
		}

		const ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
		if (!camera) {
			return item.worldMatrix;
		}

		ECSWorld& world = *item.world;
		const auto* transform = world.TryGetComponent<TransformComponent>(item.entity);
		const auto* billboard = world.TryGetComponent<BillboardComponent>(item.entity);

		if (!transform || !billboard || billboard->axes.empty()) {
			return item.worldMatrix;
		}

		const Matrix4x4 parentWorldMatrix = ResolveParentWorldMatrix(world, item.entity);
		const Vector3 objectWorldPos = item.worldMatrix.GetTranslationValue();

		// カメラ方向を見るワールド回転
		const Quaternion desiredWorldRotation = MakeCameraBillboardRotation(*camera, objectWorldPos);

		// 親のワールド回転の逆を求めて、ローカル空間での目標回転・目標方向を計算する
		Quaternion parentWorldRot = ExtractRotationNoScale(parentWorldMatrix);
		Quaternion invParentRot = Quaternion::Inverse(parentWorldRot);
		Quaternion desiredLocalRotation = invParentRot * desiredWorldRotation;

		// ワールド空間でのforward方向
		const Matrix4x4 cameraWorldMatrix = Matrix4x4::Inverse(camera->matrices.viewMatrix);
		Vector3 cameraWorldPos = cameraWorldMatrix.GetTranslationValue();
		Vector3 worldForward = Vector3::NormalizeOr(cameraWorldPos - objectWorldPos, { 0.0f, 0.0f, 1.0f });

		// ローカル空間でのforward方向
		Vector3 localForward = Vector3::Transform(worldForward, Quaternion::MakeRotateMatrix(invParentRot));
		localForward = Vector3::NormalizeOr(localForward, { 0.0f, 0.0f, 1.0f });

		Quaternion billboardRotation = ApplyAxisMask(transform->localRotation, desiredLocalRotation, *billboard, localForward);

		const Matrix4x4 localMatrix = Matrix4x4::MakeAffineMatrix(transform->localScale, billboardRotation, transform->localPos);
		return localMatrix * parentWorldMatrix;
	}

} // namespace Engine::RenderBillboard