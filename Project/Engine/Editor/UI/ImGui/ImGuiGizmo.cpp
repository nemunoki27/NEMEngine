#include "ImGuiHelpers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

namespace {

	// 操作モードをImGuizmoへ変換する
	ImGuizmo::OPERATION ToImGuizmoOperation(Engine::SceneViewManipulatorMode mode) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate:
			return ImGuizmo::TRANSLATE;
		case Engine::SceneViewManipulatorMode::Rotate:
			return ImGuizmo::ROTATE;
		case Engine::SceneViewManipulatorMode::Scale:
			return ImGuizmo::SCALE;
		case Engine::SceneViewManipulatorMode::None:
		default:
			break;
		}
		return static_cast<ImGuizmo::OPERATION>(0);
	}
	// 2Dアフィン行列を平行移動とZ軸のみの回転と拡縮に分解する
	bool DecomposeAffine2D(
		const Engine::Matrix4x4& matrix, Engine::Vector3& outPos, float& outRotationZ, Engine::Vector3& outScale) {

		constexpr float kEps = 1e-6f;

		outPos = matrix.GetTranslationValue();
		if (!std::isfinite(outPos.x) || !std::isfinite(outPos.y) || !std::isfinite(outPos.z)) {
			return false;
		}

		Engine::Vector2 axisX(matrix.m[0][0], matrix.m[0][1]);
		Engine::Vector2 axisY(matrix.m[1][0], matrix.m[1][1]);

		outScale.x = axisX.Length();
		outScale.y = axisY.Length();
		outScale.z = 1.0f;

		if (!std::isfinite(outScale.x) || !std::isfinite(outScale.y)) {
			return false;
		}
		if (outScale.x <= kEps || outScale.y <= kEps) {
			return false;
		}

		axisX /= outScale.x;
		axisY /= outScale.y;

		const float det = axisX.x * axisY.y - axisX.y * axisY.x;
		if (!std::isfinite(det)) {
			return false;
		}

		if (det < 0.0f) {

			outScale.y = -outScale.y;
			axisY = -axisY;
		}

		outRotationZ = Math::RadToDeg(std::atan2(axisX.y, axisX.x));
		if (!std::isfinite(outRotationZ)) {
			return false;
		}

		return true;
	}
	// 2D用にQuaternionからZ回転だけを安全に取り出す
	float ExtractRotationZDegrees2D(const Engine::Quaternion& rotation) {

		constexpr float kEps = 1e-6f;

		Engine::Quaternion normalized = Engine::Quaternion::Normalize(rotation);
		Engine::Matrix4x4 rotateMatrix = Engine::Quaternion::MakeRotateMatrix(normalized);

		// ローカルX軸からZ回転を求める
		Engine::Vector2 axisX(rotateMatrix.m[0][0], rotateMatrix.m[0][1]);
		const float len = axisX.Length();
		if (len <= kEps || !std::isfinite(len)) {
			return 0.0f;
		}

		axisX /= len;

		const float angle = std::atan2(axisX.y, axisX.x);
		if (!std::isfinite(angle)) {
			return 0.0f;
		}

		return Math::RadToDeg(angle);
	}

	// 共通の行列変換とGizmo操作を実行する
	Engine::GizmoEditResult ManipulateWorld(
		const Engine::GizmoViewContext& context, const Engine::TransformComponent& transform, Engine::Matrix4x4& editedLocal) {

		Engine::GizmoEditResult result{};

		// 操作モードと描画領域を確認する
		if (context.mode == Engine::SceneViewManipulatorMode::None || !context.rect.IsValid()) {
			return result;
		}

		// 対応する操作モードを取得する
		ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
		if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
			return result;
		}

		// ローカルSRTからワールド行列を計算する
		Engine::Matrix4x4 localMatrix =
			Engine::Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation, transform.localPos);
		Engine::Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

		// ImGuizmoへ渡す行列を変換する
		float view[16]{};
		float projection[16]{};
		float matrix[16]{};
		Math::MatrixToFloat16(context.viewMatrix, view);
		Math::MatrixToFloat16(context.projectionMatrix, projection);
		Math::MatrixToFloat16(worldMatrix, matrix);

		// ギズモ操作を開始する
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(context.rect.x, context.rect.y, context.rect.width, context.rect.height);
		ImGuizmo::SetOrthographic(context.orthographic);
		ImGuizmo::AllowAxisFlip(context.allowAxisFlip);

		result.valueChanged = ImGuizmo::Manipulate(
			view, projection, operation, ImGuizmo::LOCAL, matrix, nullptr, context.useSnap ? context.snapValues : nullptr);
		result.isOver = ImGuizmo::IsOver();
		result.isUsing = ImGuizmo::IsUsing();

		// 未変更ならTransformを維持する
		if (!result.valueChanged) {
			return result;
		}

		// 編集行列を親のローカル座標へ戻す
		Engine::Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
		editedLocal = editedWorld * Engine::Matrix4x4::Inverse(context.parentWorldMatrix);

		return result;
	}
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate2D(
	[[maybe_unused]] const char* id, const GizmoViewContext& context, TransformComponent& transform) {

	// Z回転と平面内の拡縮だけを操作する
	float currentRotationZ = ExtractRotationZDegrees2D(transform.localRotation);
	TransformComponent planeTransform = transform;
	planeTransform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, currentRotationZ)));
	planeTransform.localScale.z = 1.0f;

	Matrix4x4 editedLocal{};
	GizmoEditResult result = ManipulateWorld(context, planeTransform, editedLocal);
	if (!result.valueChanged) {
		return result;
	}

	Vector3 pos{};
	Vector3 scale{};
	float rotationZ = 0.0f;
	if (!DecomposeAffine2D(editedLocal, pos, rotationZ, scale)) {
		result.valueChanged = false;
		return result;
	}

	// Z回転の角度を連続させる
	float nextRotationZ = Math::MakeContinuousAngleDegrees(rotationZ, currentRotationZ);

	// 編集結果を反映
	transform.localPos.x = pos.x;
	transform.localPos.y = pos.y;
	transform.localPos.z = pos.z;
	transform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, nextRotationZ)));
	transform.localScale.x = scale.x;
	transform.localScale.y = scale.y;
	transform.isDirty = true;
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate3D(
	[[maybe_unused]] const char* id, const GizmoViewContext& context, TransformComponent& transform) {

	Matrix4x4 editedLocal{};
	GizmoEditResult result = ManipulateWorld(context, transform, editedLocal);
	if (!result.valueChanged) {
		return result;
	}

	Vector3 pos{};
	Vector3 scale{};
	Quaternion rotation{};
	if (!Engine::DecomposeAffine3D(editedLocal, pos, rotation, scale)) {
		result.valueChanged = false;
		return result;
	}
	// 編集結果を反映
	transform.localPos = pos;
	transform.localRotation = rotation;
	transform.localScale = scale;
	transform.isDirty = true;
	return result;
}
