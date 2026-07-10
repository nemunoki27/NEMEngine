#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleTypes.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleEmitterComponent struct
	//============================================================================
	// パーティクルエフェクトの再生
	struct ParticleEmitterComponent {

		// エフェクトアセット
		AssetID effect{};

		// 再生フラグ
		bool playing = true;
		// 編集中でもプレビュー再生するか
		bool playInEditMode = true;
		// エミッター形状をSceneViewへ描画するか
		bool drawEmitterShape = false;

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Add;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;

		// エミッターの経過時間
		float runtimeTime = 0.0f;
		// 単発再生中か、ループを無視して発生継続時間分だけ発生する
		bool runtimeOneShot = false;
		// 次の発生までのタイマー
		float runtimeEmitTimer = 0.0f;
		// 生存中の粒子
		std::vector<Particle> runtimeParticles{};
		// 粒子へ割り当てる次のID
		uint32_t runtimeNextParticleID = 0;
		// 粒子IDごとのトレイル軌跡点、ワールド空間で記録する
		std::unordered_map<uint32_t, std::vector<ParticleTrailPoint>> runtimeTrails{};
		// アセットから反映した描画設定、Systemが更新し描画側が参照する
		ParticleRenderSettings runtimeRenderSettings{};
	};

	// json変換
	void from_json(const nlohmann::json& in, ParticleEmitterComponent& component);
	void to_json(nlohmann::json& out, const ParticleEmitterComponent& component);

	ENGINE_REGISTER_COMPONENT(ParticleEmitterComponent, "ParticleEmitter");
} // Engine
