#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmitterStructures.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticlePhaseStructures.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleSourceSettings.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleSurfaceSettings.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleCustomDataSettings.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleFeedbackSettings.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	// CPUとGPUの実行方式
	enum class ParticleSimulationTarget : uint8_t { CPU, GPU };

	enum class ParticleTrailMode : uint8_t { Particle, Ribbon };
	enum class ParticleTrailTextureMode : uint8_t { Stretch, Tile };

	// 粒子軌跡と発生順リボンの設定
	struct ParticleTrailSettings {

		// 死亡後保持で使用する軌跡点の既定寿命
		static constexpr float kDefaultPointLifetime = 0.8f;

		// トレイルを描画するか
		bool enabled = false;
		// トレイル元の形状を描画するか
		bool drawSource = true;
		// 粒子消滅後もトレイルを残すか
		bool keepAfterParticleDeath = false;
		ParticleTrailMode mode = ParticleTrailMode::Particle;
		ParticleTrailTextureMode textureMode = ParticleTrailTextureMode::Stretch;
		float tileLength = 0.2f;
		uint32_t maxTrails = 1024;
		uint32_t memoryBudgetMB = 32;
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
		// 軌跡点を保持する秒数
		float pointLifetime = kDefaultPointLifetime;
		// トレイル専用マテリアル、未設定なら粒子と同じものを使う
		AssetID material{};
		// トレイル専用マテリアルのテクスチャ上書き
		ParticlePhaseMaterialSettings materialSettings{};
	};

	// 1つのParticleSystemに保存する編集設定
	struct ParticleSystemSettings {

		PrimitiveRenderSpace space = PrimitiveRenderSpace::World3D;
		// ループ再生するか
		bool looping = true;

		// エミッター設定
		ParticleEmitterSettings emitter{};
		// 基本形状以外を使うGPU発生元
		ParticleSourceSettings source{};
		// 衝突イベントと実行時の描き込み
		ParticleFeedbackSettings feedback{};
		// 粒子表面の照明と深度フェード
		ParticleSurfaceSettings surface{};

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
		// ブレンドモード
		BlendMode blendMode = BlendMode::Add;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;
		// カメラ方向へ向ける回転軸、BillboardComponentと同じ軸マスク方式
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};

		// 粒子の一生を区切るフェーズのリスト、必ず1つ以上持つ
		std::vector<ParticlePhaseSettings> phases;
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
		// ブレンドモード
		BlendMode blendMode = BlendMode::Add;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;
		// カメラ方向へ向ける回転軸
		std::vector<Axis> billboardAxes{ Axis::X, Axis::Y, Axis::Z };
		// トレイル設定
		ParticleTrailSettings trail{};
		// 粒子表面の照明と深度フェード
		ParticleSurfaceSettings surface{};
		// 描画Curveの所有元とフェーズ別入力
		std::shared_ptr<const ParticleSystemSettings> authoringOwner;
		std::vector<ParticleCustomDataSettings> phaseCustomData;
		// 形状アニメーションを行うか、ShapeOverLifetimeモジュールの有無で決まる
		bool shapeOverLifetime = false;
		// エミッター設定、形状のデバッグ描画で参照する
		ParticleEmitterSettings emitter{};
		// フェーズごとのマテリアル、未設定は共通マテリアルへ落とす
		std::vector<AssetID> phaseMaterials;
		// フェーズごとのマテリアル上書きと寿命アニメーション
		std::vector<ParticlePhaseMaterialSettings> phaseMaterialSettings;
	};

	// 新規Componentの設定を作る
	ParticleSystemSettings MakeDefaultParticleSystemSettings();
	// 保存用データを検証してから読み込む
	bool FromJson(const nlohmann::json& data, ParticleSystemSettings& settings);
	// 1つのSystemの設定を書き出す
	nlohmann::json ToJson(const ParticleSystemSettings& settings);
	// 共通設定から描画用データを作る
	ParticleRenderSettings MakeParticleRenderSettings(PrimitiveRenderSpace space, const ParticleSystemSettings& settings);
}
