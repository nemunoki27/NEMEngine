#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleEffectAsset structures
	//============================================================================
	// 粒子のソート方法
	enum class ParticleSortMode :
		uint8_t {

		None,
		BackToFront,
	};


	// エミッターの形状、Rect/Cone2Dは2D専用でCircle/Pointは2Dでも使える
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

	// 形状が2Dで使えるか
	inline bool IsParticleEmitterShape2D(ParticleEmitterShape shape) {

		return shape == ParticleEmitterShape::Circle || shape == ParticleEmitterShape::Rect ||
			shape == ParticleEmitterShape::Point || shape == ParticleEmitterShape::Cone2D;
	}

	// エミッター設定、発生の間隔と個数と粒子の初期状態を持つ
	struct ParticleEmitterSettings {

		// 発生形状
		ParticleEmitterShape shape = ParticleEmitterShape::Sphere;

		// 発生間隔、この秒数ごとに発生する
		float emitInterval = 0.1f;
		// 1回の発生で生成する個数
		ParticleValue<uint32_t> emitCount{ 4 };
		// これ以上発生できない上限
		uint32_t maxParticles = 1024;

		// 粒子の寿命
		ParticleValue<float> lifetime{ 1.0f };
		// 初速
		ParticleValue<float> speed{ 1.6f };

		// Sphere/Hemisphereの半径、球面上から外向きに飛ぶ
		float sphereRadius = 0.5f;
		// Boxの大きさと射出を有効にする面
		Vector3 boxSize = Vector3::AnyInit(1.0f);
		bool boxFacePosX = true;
		bool boxFaceNegX = true;
		bool boxFacePosY = true;
		bool boxFaceNegY = true;
		bool boxFacePosZ = true;
		bool boxFaceNegZ = true;
		// Torusの主半径と管半径
		float torusRadius = 1.0f;
		float torusThickness = 0.2f;
		// Circleの半径と円弧角度
		float circleRadius = 1.0f;
		float circleArc = 360.0f;
		// Coneの開き角と底面半径
		float coneAngle = 25.0f;
		float coneRadius = 0.5f;
		// Pointの射出方向
		Vector3 pointDirection = Vector3(0.0f, 1.0f, 0.0f);
		// Rectの大きさと射出を有効にする辺、2D専用
		Vector2 rectSize = Vector2::AnyInit(1.0f);
		bool rectEdgePosX = true;
		bool rectEdgeNegX = true;
		bool rectEdgePosY = true;
		bool rectEdgeNegY = true;
	};

	// 粒子ごとのリボントレイル設定
	struct ParticleTrailSettings {

		// トレイルを描画するか
		bool enabled = false;
		// 1粒子が保持する軌跡点の上限
		int32_t maxPoints = 16;
		// 軌跡点を追加する最小移動距離
		float minDistance = 0.05f;
		// リボンの幅
		float width = 0.1f;
	};

	// モジュール1つ分の定義、idでモジュールを引きparamsは各モジュールが解釈する
	struct ParticleEffectModuleEntry {

		// モジュールの識別子
		std::string id;
		// モジュールごとのパラメータ
		nlohmann::json params = nlohmann::json::object();
	};

	// パーティクルエフェクトアセットの情報
	struct ParticleEffectAsset {

		// アセットID
		AssetID guid{};
		// エフェクトの名前
		std::string name;
		// スキーマバージョン
		uint32_t version = 1;

		// エミッターの再生時間、ループ時はこの周期で折り返す
		float duration = 2.0f;
		// ループ再生するか
		bool looping = true;

		// エミッター設定
		ParticleEmitterSettings emitter{};

		// 描画空間、2DはPlane/Ringのみ対応する
		PrimitiveRenderSpace space = PrimitiveRenderSpace::World3D;
		// 粒子の形状
		PrimitiveType shape = PrimitiveType::Plane;
		// 形状ごとのパラメータ
		PrimitivePlaneParams plane{};
		PrimitiveCrossPlaneParams crossPlane{};
		PrimitiveRingParams ring{};
		PrimitiveCylinderParams cylinder{};
		PrimitiveSphereParams sphere{};
		PrimitiveHemisphereParams hemisphere{};
		PrimitiveCubeParams cube{};
		// 粒子として描画するメッシュ、設定されていれば形状より優先する
		AssetID model{};

		// 描画に使用するマテリアル
		AssetID material{};
		// 粒子のソート方法
		ParticleSortMode sortMode = ParticleSortMode::None;
		// カメラ方向へ向ける回転軸、BillboardComponentと同じ軸マスク方式
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};

		// 使用されるモジュールのリスト
		std::vector<ParticleEffectModuleEntry> modules;
	};

	// エミッターの描画設定、Systemがアセットからコンポーネントへ反映し描画側が参照する
	struct ParticleRenderSettings {

		// 描画空間
		PrimitiveRenderSpace space = PrimitiveRenderSpace::World3D;
		// 粒子の形状
		PrimitiveType shape = PrimitiveType::Plane;
		// 形状ごとのパラメータ
		PrimitivePlaneParams plane{};
		PrimitiveCrossPlaneParams crossPlane{};
		PrimitiveRingParams ring{};
		PrimitiveCylinderParams cylinder{};
		PrimitiveSphereParams sphere{};
		PrimitiveHemisphereParams hemisphere{};
		PrimitiveCubeParams cube{};
		// 粒子として描画するメッシュ
		AssetID model{};
		// 描画に使用するマテリアル
		AssetID material{};
		// 粒子のソート方法
		ParticleSortMode sortMode = ParticleSortMode::None;
		// カメラ方向へ向ける回転軸
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};
		// 形状アニメーションを行うか、ShapeOverLifetimeモジュールの有無で決まる
		bool shapeOverLifetime = false;
		// エミッター設定、形状のデバッグ描画で参照する
		ParticleEmitterSettings emitter{};
	};

	// アセットから描画設定を作る
	ParticleRenderSettings MakeParticleRenderSettings(const ParticleEffectAsset& asset);

	// json変換
	bool FromJson(const nlohmann::json& data, ParticleEffectAsset& outAsset);
	nlohmann::json ToJson(const ParticleEffectAsset& asset);
} // Engine
