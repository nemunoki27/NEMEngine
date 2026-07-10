#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ParticleTypes structures
	//============================================================================
	// 粒子1つ分の状態、エミッターローカル空間でシミュレーションする
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

		// トレイル追跡用のエミッター内で一意なID
		uint32_t id = 0;
		// 現在のフェーズ
		uint32_t phaseIndex = 0;
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
