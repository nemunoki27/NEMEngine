#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ParticleTypes structures
	//============================================================================
	// 粒子1つ分の状態、親設定中は親ローカル空間でシミュレーションする
	struct Particle {

		// 位置
		Vector3 pos = Vector3::AnyInit(0.0f);
		// 速度
		Vector3 velocity = Vector3::AnyInit(0.0f);

		// 経過時間
		float age = 0.0f;
		// 寿命
		float lifetime = 1.0f;

		// 現在の大きさ
		float size = 1.0f;
		// 現在の軸別スケール、sizeと乗算される
		Vector3 scale = Vector3::AnyInit(1.0f);

		// 現在の色
		Color4 color = Color4::White();
		// 発光色と強さ、wが強さ
		Vector4 emissive = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// アルファ棄却の閾値、この値未満のピクセルは描かれない
		float alphaReference = 0.0f;

		// 現在の回転
		Quaternion rotation = Quaternion::Identity();
		// 角速度ベクトル
		Vector3 rotationSpeed = Vector3::AnyInit(0.0f);

		// フリップブックのUVスケールとオフセット
		Vector2 uvScale = Vector2::AnyInit(1.0f);
		Vector2 uvOffset = Vector2::AnyInit(0.0f);
		// UV回転
		float uvRotation = 0.0f;
		// UV回転の中心
		Vector2 uvPivot = Vector2::AnyInit(0.0f);

		// トレイル追跡用のエミッター内で一意なID
		uint32_t id = 0;
		// 現在のフェーズ
		uint32_t phaseIndex = 0;

		// 描画とトレイルに使うワールド姿勢
		Vector3 worldPos = Vector3::AnyInit(0.0f);
		Quaternion worldRotation = Quaternion::Identity();
		Vector3 worldScale = Vector3::AnyInit(1.0f);

		// 現在追従している親の情報
		Matrix4x4 parentMatrix = Matrix4x4::Identity();
		UUID parentLocalFileID{};
		bool parentIsEmitter = false;
		bool hasParent = false;

		// 形状アニメーション用のパラメータ、形状ごとに解釈が変わる
		Vector4 shapeParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	};

	// トレイルの軌跡点、ワールド空間で記録する
	struct ParticleTrailPoint {

		// 位置
		Vector3 position = Vector3::AnyInit(0.0f);
		// 記録してからの経過時間
		float age = 0.0f;
	};
} // Engine
