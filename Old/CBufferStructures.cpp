#include "CBufferStructures.h"

using namespace SakuEngine;

//============================================================================
//	include
//============================================================================
#include <Engine/Object/Data/Transform/Transform.h>
#include <Engine/Utility/Enum/Direction.h>

// imgui
#include <imgui.h>

//============================================================================
//	CBufferStructures
//============================================================================

void TransformationMatrix::Update(const BaseTransform3D* parent, const Vector3& scale,
	const Quaternion& rotation, const Vector3& translation, bool isIgnoreParentScale,
	const std::optional<Matrix4x4>& billboardMatrix) {

	// billboardMatrixに値が入っていればbillboardMatrixでrotateを計算する
	if (billboardMatrix.has_value()) {

		Matrix4x4 scaleMatrix = Matrix4x4::MakeScaleMatrix(scale);
		Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(translation);
		Quaternion billboardRot = Quaternion::FromRotationMatrix(billboardMatrix.value());

		// 回転行列取得
		Quaternion normalizedRotation = Quaternion::Normalize(rotation);
		Matrix4x4 fullRotMat = Quaternion::MakeRotateMatrix(normalizedRotation);

		Vector3 xAxis = Vector3::TransferNormal(Direction::Get(Direction3D::Right), fullRotMat);
		Vector3 zAxis = Vector3::TransferNormal(Direction::Get(Direction3D::Forward), fullRotMat);

		// XZだけの回転行列作成
		Vector3 newZ = Vector3::Normalize(zAxis);
		Vector3 newX = Vector3::Normalize(xAxis);
		Vector3 newY = Vector3::Normalize(Vector3::Cross(newZ, newX));
		newX = Vector3::Normalize(Vector3::Cross(newY, newZ));

		// XZの回転行列からquaternionを取得
		Matrix4x4 xzRotMatrix = {
			newX.x, newX.y, newX.z, 0.0f,
			newY.x, newY.y, newY.z, 0.0f,
			newZ.x, newZ.y, newZ.z, 0.0f,
			0.0f,   0.0f,   0.0f,   1.0f };
		Quaternion xzRotation = Quaternion::FromRotationMatrix(xzRotMatrix);

		// Y軸はbillboard、XZはrotation
		Quaternion finalRotation = Quaternion::Multiply(Quaternion::Conjugate(billboardRot), xzRotation);
		finalRotation = Quaternion::Normalize(finalRotation);

		Matrix4x4 rotateMatrix = Quaternion::MakeRotateMatrix(finalRotation);
		world = scaleMatrix * rotateMatrix * translateMatrix;
	} else {

		world = Matrix4x4::MakeAxisAffineMatrix(
			scale, rotation, translation);
	}
	if (parent) {

		Matrix4x4 parentMatrix = parent->matrix.world;

		// 親のスケールの影響を受けない場合
		if (isIgnoreParentScale) {

			// 親の回転成分を正規化してスケール成分を打ち消す
			Vector3 x = Vector3(parentMatrix.m[0][0], parentMatrix.m[1][0], parentMatrix.m[2][0]).Normalize();
			Vector3 y = Vector3(parentMatrix.m[0][1], parentMatrix.m[1][1], parentMatrix.m[2][1]).Normalize();
			Vector3 z = Vector3(parentMatrix.m[0][2], parentMatrix.m[1][2], parentMatrix.m[2][2]).Normalize();

			// スケール除去した回転行列を再セット
			parentMatrix.m[0][0] = x.x; parentMatrix.m[1][0] = x.y; parentMatrix.m[2][0] = x.z;
			parentMatrix.m[0][1] = y.x; parentMatrix.m[1][1] = y.y; parentMatrix.m[2][1] = y.z;
			parentMatrix.m[0][2] = z.x; parentMatrix.m[1][2] = z.y; parentMatrix.m[2][2] = z.z;
		}
		world = world * parentMatrix;
	}
	worldInverseTranspose = Matrix4x4::Transpose(Matrix4x4::Inverse(world));
}

void SpriteMaterialForGPU::Init() {

	color = Color::White();
	useVertexColor = false;
	useAlphaColor = false;
	emissiveIntensity = 0.0f;
	alphaReference = 0.0f;
	emissionColor = Vector3::AnyInit(1.0f);
	uvTransform = Matrix4x4::MakeIdentity4x4();
	postProcessMask = 0;
}

void SpriteMaterialForGPU::ImGui() {

	ImGui::ColorEdit4("color", &color.r);
	ImGui::Text("R:%4.3f G:%4.3f B:%4.3f A:%4.3f",
		color.r, color.g,
		color.b, color.a);
}

void MSDFTextMaterialForGPU::Init() {

	color = Color::White();
	outlineColor = Color::Black();
	atlasSize = Vector2::AnyInit(512.0f);
	pixelRange = 4.0f;
	outlineWidth = 0.0f;
	softness = 0.1f;
	boldness = 0.0f;
	enableOutline = 0;
	postProcessMask = 0;
}

void MSDFTextMaterialForGPU::ImGui() {
}