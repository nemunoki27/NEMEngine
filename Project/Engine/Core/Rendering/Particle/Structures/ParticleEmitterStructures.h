#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	ParticleEmitterStructures
	//	エミッターの発生形状と設定、形状ごとの処理はIParticleEmitterShapeが担当する
	//============================================================================
	// エミッターの形状
	enum class ParticleEmitterShape :
		uint8_t {

		Sphere,
		Hemisphere,
		Box,
		Torus,
		Circle,
		Cone,
		Point,
		Rect,
		Cone2D,
	};

	// Sphere/Hemisphereのパラメータ、球面上から外向きに飛ぶ
	struct ParticleEmitterSphereParams {

		// 半径
		float radius = 0.5f;
	};

	// Boxのパラメータ
	struct ParticleEmitterBoxParams {

		// 大きさ
		Vector3 size = Vector3::AnyInit(1.0f);
		// 射出を有効にする面
		bool facePosX = true;
		bool faceNegX = true;
		bool facePosY = true;
		bool faceNegY = true;
		bool facePosZ = true;
		bool faceNegZ = true;
	};

	// Torusのパラメータ
	struct ParticleEmitterTorusParams {

		// 主半径
		float radius = 1.0f;
		// 管半径
		float thickness = 0.2f;
	};

	// Circleのパラメータ
	struct ParticleEmitterCircleParams {

		// 半径
		float radius = 1.0f;
		// 円弧角度
		float arc = 360.0f;
	};

	// Cone/Cone2Dのパラメータ
	struct ParticleEmitterConeParams {

		// 開き角
		float angle = 25.0f;
		// 底面半径
		float radius = 0.5f;
	};

	// Pointのパラメータ
	struct ParticleEmitterPointParams {

		// 射出方向
		Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);
	};

	// Rectのパラメータ、2D専用
	struct ParticleEmitterRectParams {

		// 大きさ
		Vector2 size = Vector2::AnyInit(1.0f);
		// 射出を有効にする辺
		bool edgePosX = true;
		bool edgeNegX = true;
		bool edgePosY = true;
		bool edgeNegY = true;
	};

	// エミッター設定、発生の間隔と個数と粒子の初期状態を持つ
	// 形状パラメータは切り替えで失わないよう全形状分を保持する
	struct ParticleEmitterSettings {

		// 発生形状
		ParticleEmitterShape shape = ParticleEmitterShape::Sphere;

		// 発生間隔、この秒数ごとに発生する
		float emitInterval = 0.1f;
		// 1回の発生で生成する個数
		ParticleValue<uint32_t> emitCount{ 4 };
		// これ以上発生できない上限
		uint32_t maxParticles = 1024;

		// 初速
		ParticleValue<float> speed{ 1.6f };

		// 形状ごとのパラメータ
		ParticleEmitterSphereParams sphere{};
		ParticleEmitterBoxParams box{};
		ParticleEmitterTorusParams torus{};
		ParticleEmitterCircleParams circle{};
		ParticleEmitterConeParams cone{};
		ParticleEmitterPointParams point{};
		ParticleEmitterRectParams rect{};
	};
} // Engine
