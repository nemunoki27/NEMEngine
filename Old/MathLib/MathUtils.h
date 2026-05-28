#pragma once

//============================================================================
// include
//============================================================================
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Vector4.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/Utility/Enum/Direction.h>

// windows
#include <windows.h>
#include <windef.h>
// c++
#include <cmath>
#include <numbers>
#include <vector>
#include <algorithm>

namespace SakuEngine {

	// front
	class BaseCamera;
	class GameObject3D;

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float radian = pi / 180.0f;

	//============================================================================
	// Math namespace
	// 汎用的な数学関数群
	//============================================================================

	namespace Math {

		// 軸
		enum class Axis {

			X,
			Y,
			Z
		};

		// 絶対値を返す
		float AbsFloat(float v);

		// 角度を[-π,π]範囲に折り返す
		float WrapDegree(float value);
		float WrapPi(float value);

		// クライアント座標系の矩形を作成する
		RECT MakeClientRect(const Vector2& size, const Vector2& pos);

		//============================================================================
		// 回転関連
		//============================================================================

		// 方向ベクトルからyaw角(ラジアン)を算出する
		float GetYawRadian(const Vector3& direction);

		// from→toのヨー最短方向を{-1,0,+1}で返す
		int YawShortestDirection(const Quaternion& from, const Quaternion& to);

		// from→toのヨー角の符号付き差(ラジアン)を返す
		float YawSignedDelta(const Quaternion& from, const Quaternion& to);

		// fromPos から見てtoPosが左右どちらにあるかを返す
		AnchorToDirection2D YawSideFromPos(const Vector3& fromPos, const Quaternion& fromRot, const Vector3& toPos);

		// ツイストQuaternionから指定軸の角度(ラジアン)を算出する
		float AngleFromTwist(const Quaternion& twist, Axis axis);

		// Y軸回りにベクトルを回転させる
		Vector3 RotateY(const Vector3& v, float rad);

		// 2つのクォータニオン間の角度差を度数で取得する
		float QuaternionAngleDeg(const Quaternion& rotateA, const Quaternion& rotateB);

		//============================================================================
		// 円弧関連
		//============================================================================

		// 円弧上のランダム点を生成する
		Vector3 RandomPointOnArc(const Vector3& center, const Vector3& direction,
			float radius, float halfAngle);

		// 正方形領域に収まる円弧上のランダム点を生成する
		Vector3 RandomPointOnArcInSquare(const Vector3& arcCenter, const Vector3& direction,
			float radius, float halfAngle, const Vector3& squareCenter,
			float clampHalfSize, int tryCount = 12);

		//============================================================================
		// 行列関連
		//============================================================================

		// 行列を列メジャー配列へ書き出す
		void ToColumnMajor(const Matrix4x4& matrix, float out[16]);
		// 列メジャー配列から行列へ読み込む
		void FromColumnMajor(const float in[16], Matrix4x4& matrix);
		// 行列をfloat配列に書き出す
		void ToFloatMatrix(const Matrix4x4& matrix, float out[16]);

		//============================================================================
		// 座標変換関連
		//============================================================================

		// ワールド座標をスクリーン座標へ射影する
		Vector2 ProjectToScreen(const Vector3& translation, const BaseCamera& camera);

		//============================================================================
		// 3Dオブジェクト関連
		//============================================================================

		// 3DオブジェクトのY座標指定した位置を取得する
		Vector3 GetFlattenPos3D(const GameObject3D& object, bool isWorld = true, float posY = 0.0f);

		// 3Dオブジェクト同士の距離を取得する
		float GetDistance3D(const GameObject3D& object0, const GameObject3D& object1,
			bool isWorld = true, bool isFlattenPos = false);

		// fromからtoへの方向ベクトルを取得する
		// ignoreAxisは無視する軸
		Vector3 GetDirection3D(const GameObject3D& from, const GameObject3D& to,
			bool isWorld = true, const std::vector<Axis>& ignoreAxis = { Axis::Y });

		// 進行方向に向けてオブジェクトを回転(補間)させる
		void RotateToDirection3D(GameObject3D& object, const Vector3& direction, float lerpRate = 1.0f);
		// 指定座標を向くようにオブジェクトを回転(補間)させる
		void LookTarget3D(GameObject3D& object, const Vector3& targetPos,
			float lerpRate = 1.0f, bool useScaledDeltaTime = true);
	}

}; // SakuEngine
