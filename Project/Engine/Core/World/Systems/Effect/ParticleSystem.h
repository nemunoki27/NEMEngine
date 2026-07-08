#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/ParticleModule.h>

// c++
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

	// front
	struct ParticleEmitterComponent;

	//============================================================================
	//	ParticleSystem class
	//	エフェクトアセットのモジュールでパーティクルを発生、更新する
	//============================================================================
	class ParticleSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSystem() = default;
		~ParticleSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "ParticleSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// アセットから構築したモジュール一式、アセットID単位で共有する
		struct EffectRuntime {

			ParticleEffectAsset asset{};
			std::vector<std::unique_ptr<IParticleModule>> modules;
			bool valid = false;

			// ホットリロード用のファイル情報
			std::filesystem::path path{};
			std::filesystem::file_time_type lastWriteTime{};
			// エディター編集の適用済みバージョン
			uint64_t appliedEditVersion = 0;
		};

		//--------- variables ----------------------------------------------------

		// ホットリロードの確認間隔
		const float kReloadCheckInterval = 0.5f;

		// アセットIDからエフェクトへのマップ
		std::unordered_map<AssetID, EffectRuntime> effectCache_;

		// ホットリロードの確認タイマー
		float reloadCheckTimer_ = 0.0f;

		// トレイル整理用の生存ID、毎フレーム使い回す
		std::unordered_set<uint32_t> aliveTrailIDs_;

		//--------- functions ----------------------------------------------------

		// エフェクトを取得する、未ロードならアセットを読み込みモジュールを構築する
		const EffectRuntime* ResolveEffect(SystemContext& context, AssetID effectID, bool checkReload);
		// アセットを読み込んでモジュールを構築する
		EffectRuntime LoadEffect(SystemContext& context, AssetID effectID) const;
		// アセットのモジュール定義からモジュールを構築する
		void BuildModules(EffectRuntime& runtime) const;
		// トレイルの軌跡点をワールド空間で記録し、死亡した粒子の軌跡を破棄する
		void RecordTrails(ECSWorld& world, const Entity& entity,
			ParticleEmitterComponent& emitter, const ParticleTrailSettings& trail);
		// エミッター形状から発生位置と方向と初期状態を決める
		void InitEmitterParticles(std::span<Particle> newborn, const ParticleEmitterSettings& settings, bool is2D) const;
		// エミッター形状をデバッグ線で描画する
		void DrawEmitterShape(ECSWorld& world, const Entity& entity,
			const ParticleEmitterSettings& settings, bool is2D) const;
	};
} // Engine
