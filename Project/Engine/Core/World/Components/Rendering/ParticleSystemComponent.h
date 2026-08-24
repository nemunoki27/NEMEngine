#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Rendering/Particle/ParticleTypes.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	// ParticleSystem停止時のEntity操作
	enum class ParticleSystemStopAction :
		uint8_t {

		None,
		Disable,
		Destroy,
	};

	// 停止要求後に既存粒子を残すか
	enum class ParticleSystemStopBehavior :
		uint8_t {

		StopEmittingAndClear,
		StopEmitting,
	};

	// 親空間をエフェクト設定から上書きする方式
	enum class ParticleSystemSimulationSpace :
		uint8_t {

		EffectAsset,
		Local,
		World,
		Custom,
	};

	// ParticleSystemへ積む再生要求
	enum class ParticleSystemCommandType :
		uint8_t {

		Play,
		Pause,
		Stop,
		Clear,
		Restart,
	};

	// パーティクルグループの実行状態
	struct ParticleGroupRuntimeState {

		UUID groupID{};
		float emitTimer = 0.0f;
		bool emitted = false;
		std::vector<Particle> particles{};
		uint32_t nextParticleID = 0;
		std::unordered_map<uint32_t, ParticleTrailRuntime> trails{};
		ParticleRenderSettings renderSettings{};
	};

	// ParticleEffectアセット1つ分の実行状態
	struct ParticleEffectInstanceRuntime {

		AssetID effect{};
		bool oneShot = false;
		bool emissionStopped = false;
		float runtimeGroupEmitTimer = 0.0f;
		bool runtimeGroupEmitted = false;
		ParticleEffectGroupEmissionMode runtimeGroupEmissionMode =
			ParticleEffectGroupEmissionMode::Independent;
		AssetID runtimeEffectID{};
		uint64_t runtimeEffectRevision = 0;
		std::vector<ParticleGroupRuntimeState> runtimeGroups{};
	};

	// C#とエディターから受け取る再生要求
	struct ParticleSystemCommand {

		ParticleSystemCommandType type = ParticleSystemCommandType::Play;
		ParticleSystemStopBehavior stopBehavior =
			ParticleSystemStopBehavior::StopEmitting;
		bool oneShot = false;
	};

	// チャンク外で所有するParticleSystemの実行時データ
	struct ParticleSystemRuntimeData {

		bool initialized = false;
		bool playing = false;
		bool paused = false;
		bool stopped = true;
		bool stopActionPending = false;
		AssetID activeEffect{};
		ParticleEffectInstanceRuntime effect{};
		std::vector<ParticleSystemCommand> commands{};
	};

	struct ParticleSystemRuntimeStorageTag;
	using ParticleSystemRuntimeStorage =
		GenerationalPool<ParticleSystemRuntimeData, ParticleSystemRuntimeStorageTag>;
	using ParticleSystemRuntimeHandle = ParticleSystemRuntimeStorage::Handle;

	// ECSチャンクには世代付きハンドルだけを保持する
	struct ParticleSystemRuntimeComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		ParticleSystemRuntimeHandle handle{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, ParticleSystemRuntimeComponent& component);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, ParticleSystemRuntimeComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, ParticleSystemRuntimeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, ParticleSystemRuntimeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const ParticleSystemRuntimeComponent& component, nlohmann::json& out);
	};

	//============================================================================
	//	ParticleSystemComponent struct
	//============================================================================
	// Entity上でParticleEffectアセット1つを再生するコンポーネント
	struct ParticleSystemComponent {

		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Render;

		AssetID effect{};
		UUID customSimulationTarget{};
		float playbackSpeed = 1.0f;
		int32_t layer = 0;
		int32_t order = 0;
		ParticleSystemStopAction stopAction = ParticleSystemStopAction::None;
		ParticleSystemSimulationSpace simulationSpace =
			ParticleSystemSimulationSpace::EffectAsset;
		bool enabled = true;
		bool playOnAwake = true;
		bool playInEditMode = true;
		bool useUnscaledTime = false;
		bool drawEmitterShape = false;
		bool visible = true;

		static void OnAdded(
			ECSWorld& world, const Entity& entity, ParticleSystemComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, ParticleSystemComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, ParticleSystemComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, ParticleSystemComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const ParticleSystemComponent& component, nlohmann::json& out);
	};

	// ParticleSystemのRuntimeデータを返す
	ParticleSystemRuntimeData* TryGetParticleSystemRuntime(
		ECSWorld& world, const Entity& entity);
	const ParticleSystemRuntimeData* TryGetParticleSystemRuntime(
		const ECSWorld& world, const Entity& entity);
	// 再生操作を次のParticleSystem更新へ積む
	void RequestParticleSystemPlay(ECSWorld& world, const Entity& entity,
		bool oneShot = false);
	void RequestParticleSystemPause(ECSWorld& world, const Entity& entity);
	void RequestParticleSystemStop(ECSWorld& world, const Entity& entity,
		ParticleSystemStopBehavior behavior = ParticleSystemStopBehavior::StopEmitting);
	void RequestParticleSystemClear(ECSWorld& world, const Entity& entity);
	void RequestParticleSystemRestart(ECSWorld& world, const Entity& entity,
		bool oneShot = false);
	bool IsParticleSystemPlaying(const ECSWorld& world, const Entity& entity);
	bool IsParticleSystemEmitting(const ECSWorld& world, const Entity& entity);
	bool IsParticleSystemPaused(const ECSWorld& world, const Entity& entity);
	bool IsParticleSystemStopped(const ECSWorld& world, const Entity& entity);
	bool IsParticleSystemAlive(const ECSWorld& world, const Entity& entity);
	int32_t GetParticleSystemParticleCount(
		const ECSWorld& world, const Entity& entity);

	// json変換
	void from_json(const nlohmann::json& in, ParticleSystemComponent& component);
	void to_json(nlohmann::json& out, const ParticleSystemComponent& component);

} // Engine
