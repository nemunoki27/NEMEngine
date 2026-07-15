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
	//	ParticleGroupRuntimeState struct
	//============================================================================
	// パーティクルグループの実行状態
	struct ParticleGroupRuntimeState {

		// アセット内のグループID
		UUID groupID{};
		// エミッターの経過時間
		float time = 0.0f;
		// 次の発生までのタイマー
		float emitTimer = 0.0f;
		// 生存中の粒子
		std::vector<Particle> particles{};
		// 粒子へ割り当てる次のID
		uint32_t nextParticleID = 0;
		// 粒子IDごとのトレイル状態
		std::unordered_map<uint32_t, ParticleTrailRuntime> trails{};
		// アセットから反映した描画設定
		ParticleRenderSettings renderSettings{};
	};

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

		// 単発再生中か、ループを無視して発生継続時間分だけ発生する
		bool runtimeOneShot = false;
		// 同時発生の次回タイマー
		float runtimeGroupEmitTimer = 0.0f;
		// 単発の同時発生を実行済みか
		bool runtimeGroupEmitted = false;
		// エフェクトの描画空間
		PrimitiveRenderSpace runtimeSpace = PrimitiveRenderSpace::World3D;
		// 実行状態を構築したエフェクトID
		AssetID runtimeEffectID{};
		// 実行状態へ反映したエフェクト定義の世代
		uint64_t runtimeEffectRevision = 0;
		// グループごとの実行状態
		std::vector<ParticleGroupRuntimeState> runtimeGroups{};
	};

	// json変換
	void from_json(const nlohmann::json& in, ParticleEmitterComponent& component);
	void to_json(nlohmann::json& out, const ParticleEmitterComponent& component);

	ENGINE_REGISTER_COMPONENT(ParticleEmitterComponent, "ParticleEmitter");
} // Engine
