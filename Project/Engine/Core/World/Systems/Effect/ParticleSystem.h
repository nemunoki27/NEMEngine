#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>

// c++
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

	// front
	struct ParticleSystemComponent;
	struct ParticleSystemRuntimeData;
	struct ParticleEffectInstanceRuntime;
	struct ParticleGroupRuntimeState;

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

		// 同じ処理方式で連続するモジュール
		struct ModuleExecutionGroup {

			ParticleModuleExecutionMode mode = ParticleModuleExecutionMode::None;
			std::vector<IParticleModule*> modules;
		};

		// フェーズ1つ分の実行データ
		struct PhaseRuntime {

			ParticleValue<float> lifetime{ 1.0f };
			ParticleLifeEndMode lifeEndMode = ParticleLifeEndMode::Kill;
			ParticlePhaseParentSettings parentSettings{};
			std::vector<std::unique_ptr<IParticleModule>> modules;
			std::vector<ModuleExecutionGroup> spawnExecution;
			std::vector<ModuleExecutionGroup> updateExecution;
			bool hasSpawnBatch = false;
			bool hasUpdateBatch = false;
		};

		// グループ1つ分の実行定義
		struct GroupRuntime {

			UUID id{};
			std::vector<PhaseRuntime> phases;
			bool hasUpdateBatch = false;
		};

		// エミッターごとに解決したフェーズの親姿勢
		struct ParentRuntime {

			Matrix4x4 matrix = Matrix4x4::Identity();
			Quaternion rotation = Quaternion::Identity();
			Vector3 scale = Vector3::AnyInit(1.0f);
			bool resolved = false;
		};

		// アセットから構築したフェーズ一式、アセットID単位で共有する
		struct EffectRuntime {

			ParticleEffectAsset asset{};
			std::vector<GroupRuntime> groups;
			bool valid = false;
			uint64_t revision = 1;

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
		// フェーズの親姿勢解決用、エミッターごとに使い回す
		std::vector<ParentRuntime> parentRuntimes_;

		//--------- functions ----------------------------------------------------

		// エフェクトを取得する、未ロードならアセットを読み込みフェーズを構築する
		const EffectRuntime* ResolveEffect(SystemContext& context, AssetID effectID, bool checkReload);
		// アセットを読み込んでフェーズを構築する
		EffectRuntime LoadEffect(SystemContext& context, AssetID effectID) const;
		// アセットのグループとフェーズから実行定義を構築する
		void BuildGroups(EffectRuntime& runtime) const;
		// コンポーネントの再生要求を実行状態へ反映する
		void ProcessCommands(const ParticleSystemComponent& component,
			ParticleSystemRuntimeData& runtime) const;
		// ParticleEffectを先頭から再生する
		void StartEffect(ParticleSystemRuntimeData& runtime,
			AssetID effectID, bool oneShot) const;
		// ParticleEffectの描画状態を破棄する
		void ClearEffect(ParticleSystemRuntimeData& runtime) const;
		// 再生完了時のEntity操作を適用する
		void ApplyStopAction(ECSWorld& world, const Entity& entity,
			ParticleSystemComponent& component, ParticleSystemRuntimeData& runtime,
			WorldMode mode) const;
		// ParticleEffect実行状態をアセットのグループ順へ同期する
		bool SynchronizeRuntimeGroups(
			ParticleEffectInstanceRuntime& instance, const EffectRuntime& effect) const;
		// ParticleEffectの再生状態を先頭へ戻す
		void RestartEffectInstance(ParticleEffectInstanceRuntime& instance, const ParticleEffectAsset& asset) const;
		// 同時発生を行うフレームか判定する
		bool UpdateGroupEmission(ParticleEffectInstanceRuntime& instance,
			const ParticleEffectAsset& asset, float deltaTime, bool emissionEnabled) const;
		// ParticleEffect1つ分を更新する
		bool UpdateEffectInstance(ECSWorld& world,
			ParticleEffectInstanceRuntime& instance, const Matrix4x4& emitterWorld,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			SystemContext& context, float deltaTime, bool updateSimulation,
			bool emissionEnabled, bool drawEmitterShape, bool checkReload);
		// グループ1つ分の粒子とトレイルを更新する
		void UpdateGroup(ECSWorld& world, const Matrix4x4& emitterWorld,
			ParticleGroupRuntimeState& state, const ParticleEffectAsset& asset,
			const ParticleEffectGroup& group, const GroupRuntime& runtime,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			float deltaTime, bool updateSimulation, bool simultaneousEmit, bool emissionEnabled,
			bool oneShot, bool drawEmitterShape);
		// 寿命が尽きた粒子をLifeEndModeに従って遷移させる、破棄するならfalse
		bool AdvancePhaseOnLifeEnd(Particle& particle, const std::vector<PhaseRuntime>& phases) const;
		// Batchを含む更新計画をフェーズごとの連続範囲へ適用する
		void UpdatePhaseModules(std::vector<Particle>& particles, const GroupRuntime& group, float deltaTime) const;
		// 1粒子へ発生モジュールを適用する
		void ApplySpawnModules(Particle& particle, const PhaseRuntime& phase) const;
		// 1粒子へ更新モジュールを適用する
		void ApplyUpdateModules(Particle& particle, const PhaseRuntime& phase, float deltaTime) const;
		// 発生モジュールの実行計画を粒子範囲へ適用する
		void ExecuteSpawnModules(std::span<Particle> particles, const PhaseRuntime& phase) const;
		// 更新モジュールの実行計画を粒子範囲へ適用する
		void ExecuteUpdateModules(std::span<Particle> particles,
			const PhaseRuntime& phase, float deltaTime) const;
		// 通常再生とエフェクト作成ツールで使用する親設定を切り替える
		const ParticlePhaseParentSettings& ResolveParticleParentSettings(const PhaseRuntime& phase,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings) const;
		// 現在フェーズの親設定を粒子へ反映する
		void UpdateParticleParent(Particle& particle, const ParticlePhaseParentSettings& settings,
			const ParentRuntime& parent, bool preserveWorldRotationScale = true) const;
		// 全粒子の親行列と描画用ワールド姿勢を更新する
		void UpdateParticleParents(std::vector<Particle>& particles, const GroupRuntime& group,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			const std::vector<ParentRuntime>& parents) const;
		// 各フェーズの親姿勢をエミッター単位で解決する
		void ResolveParticleParents(ECSWorld& world, const Matrix4x4& emitterWorld, const GroupRuntime& group,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			std::vector<ParentRuntime>& outParents) const;
		// 粒子の描画用ワールド姿勢を更新する
		void RefreshParticleWorldTransform(Particle& particle, const ParentRuntime* parent) const;
		// トレイルの軌跡点をワールド空間で記録し、死亡した粒子から切り離す
		void RecordTrails(ParticleGroupRuntimeState& state,
			const ParticleTrailSettings& trail, float deltaTime);
		// 粒子消滅後に退避したトレイル所有者を更新する
		void UpdateDetachedTrailOwners(ParticleGroupRuntimeState& state, const GroupRuntime& group,
			const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
			const std::vector<ParentRuntime>& parents, const ParticleTrailSettings& trail, float deltaTime) const;
		// エミッター形状から発生位置と方向と初期状態を決める、firstSpawnIndexは発生順の連番の開始値
		void InitEmitterParticles(std::span<Particle> newborn, const ParticleEmitterSettings& settings,
			const ParticleValue<float>& lifetime, bool is2D, uint32_t firstSpawnIndex) const;
		// エミッター形状をデバッグ線で描画する
		void DrawEmitterShape(const Matrix4x4& emitterWorld,
			const ParticleEmitterSettings& settings, bool is2D) const;
	};
} // Engine
