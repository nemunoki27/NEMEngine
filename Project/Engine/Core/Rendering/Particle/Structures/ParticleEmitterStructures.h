#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <cstddef>

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
	inline constexpr size_t kParticleEmitterShapeCount =
		static_cast<size_t>(ParticleEmitterShape::Cone2D) + 1;

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

	// 発生位置の決め方
	enum class ParticleEmitterSpawnMode :
		uint8_t {

		Random,       // ランダムに選ぶ
		EvenPerFrame, // 1回の発生数で等間隔に並べる
		Progressive,  // 発生させるごとに角度を進める
	};

	// 発生速度の向きの決め方
	enum class ParticleEmitterVelocityMode :
		uint8_t {

		Normal,    // 円周の法線方向
		NextPoint, // 次の発生点へ向ける
		PrePoint,  // 前の発生点へ向ける
	};

	// 発生1回分の連番情報
	struct ParticleSpawnIndex {

		// エミッター全体での発生連番
		uint32_t global = 0;
		// この発生回内でのインデックスと発生数
		uint32_t batchIndex = 0;
		uint32_t batchCount = 1;
	};

	// Circleのパラメータ
	struct ParticleEmitterCircleParams {

		// 半径
		float radius = 1.0f;
		// 円弧の角度範囲、度数法で0度跨ぎにも対応する
		float angleMin = 0.0f;
		float angleMax = 360.0f;
		// 角度を進める方向を反転するか
		bool clockwise = false;

		// 発生位置の決め方
		ParticleEmitterSpawnMode spawnMode = ParticleEmitterSpawnMode::Random;
		// 角度を進めるモードの1発生あたりのステップ角度
		float stepAngle = 10.0f;

		// 発生速度の向きの決め方
		ParticleEmitterVelocityMode velocityMode = ParticleEmitterVelocityMode::Normal;
		// 角度を進めて最初に戻る瞬間は前の区間の向きを使う
		bool usePrevSegmentDirectionOnWrap = false;
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
		// 発生座標オフセット
		ParticleValue<Vector3> emitOffset{};

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
