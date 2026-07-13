#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmitterStructures.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticlePhaseStructures.h>
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

	// 粒子ごとのリボントレイル設定
	struct ParticleTrailSettings {

		// トレイルを描画するか
		bool enabled = false;
		// 1粒子が保持する軌跡点の上限
		int32_t maxPoints = 16;
		// 軌跡点を追加する最小移動距離
		float minDistance = 0.05f;
		// リボンの幅、先頭と尻尾で補間する
		float startWidth = 0.1f;
		float endWidth = 0.1f;
		// リボンの色、先頭と尻尾で補間して粒子色へ掛ける
		Color4 startColor = Color4::White();
		Color4 endColor = Color4(1.0f, 1.0f, 1.0f, 0.0f);
		// 軌跡点の寿命、この秒数を超えた点は消える、0以下で無制限
		float pointLifetime = 0.0f;
		// トレイル専用マテリアル、未設定なら粒子と同じものを使う
		AssetID material{};
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
		// ブレンドモード
		BlendMode blendMode = BlendMode::Add;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;
		// カメラ方向へ向ける回転軸、BillboardComponentと同じ軸マスク方式
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};

		// 粒子の一生を区切るフェーズのリスト、必ず1つ以上持つ
		std::vector<ParticleEffectPhase> phases;
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
		// ブレンドモード
		BlendMode blendMode = BlendMode::Add;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;
		// カメラ方向へ向ける回転軸
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};
		// 形状アニメーションを行うか、ShapeOverLifetimeモジュールの有無で決まる
		bool shapeOverLifetime = false;
		// エミッター設定、形状のデバッグ描画で参照する
		ParticleEmitterSettings emitter{};
		// フェーズごとのマテリアル、未設定は共通マテリアルへ落とす
		std::vector<AssetID> phaseMaterials;
		// フェーズごとのマテリアル上書きと寿命アニメーション
		std::vector<ParticlePhaseMaterialSettings> phaseMaterialSettings;
	};

	// アセットから描画設定を作る
	ParticleRenderSettings MakeParticleRenderSettings(const ParticleEffectAsset& asset);

	// json変換
	bool FromJson(const nlohmann::json& data, ParticleEffectAsset& outAsset);
	nlohmann::json ToJson(const ParticleEffectAsset& asset);
} // Engine
