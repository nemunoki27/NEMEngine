#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Rendering/Particle/ParticleStorage.h>
#include <Engine/Core/Rendering/Particle/ParticleTrailStorage.h>
#include "ParticleSystemSettingsStorage.h"
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <unordered_map>
#include <vector>
#include <random>

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

		PhaseSettings,
		Local,
		World,
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

	struct ParticleSystemDefinition;
	struct ParticleGPUState;

	// 1つのエミッターの実行状態
	struct ParticleEmitterRuntime {

		float emitTimer = 0.0f;
		bool emitted = false;
		ParticleStorage particles{};
		std::shared_ptr<ParticleGPUState> gpu;
		uint64_t nextParticleID = 0;
		ParticleTrailStorage trails{};
		ParticleTrailHandle ribbon{};
		uint64_t trailSerial = 0;
		ParticleRenderSettings renderSettings{};
	};

	// 再生単位の定義と乱数列
	struct ParticleInstanceState {

		std::mt19937 randomGenerator{};
		uint32_t randomSeed = 0;
		bool randomInitialized = false;
		bool useAutoRandomSeed = true;
		bool oneShot = false;
		bool emissionStopped = false;
		ParticleSimulationTarget target = ParticleSimulationTarget::CPU;
		std::shared_ptr<const ParticleSystemDefinition> definition;
		std::shared_ptr<const ParticleSystemSettings> rejectedSettings;
		std::string rejectedReason;
		ParticleEmitterRuntime emitter;
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
		ParticleInstanceState instance{};
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
	// Entity上で1つのエミッターを再生するコンポーネント
	struct ParticleSystemComponent {

		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Render;

		ParticleSystemSettingsHandle settings{};
		float playbackSpeed = 1.0f;
		int32_t layer = 0;
		int32_t order = 0;
		// Cameraの描画対象レイヤー
		uint32_t renderingLayerMask = 1u;
		ParticleSystemStopAction stopAction = ParticleSystemStopAction::None;
		ParticleSystemSimulationSpace simulationSpace =
			ParticleSystemSimulationSpace::PhaseSettings;
		ParticleSimulationTarget simulationTarget = ParticleSimulationTarget::CPU;
		bool enabled = true;
		bool playOnAwake = true;
		bool playInEditMode = true;
		bool useUnscaledTime = false;
		bool useAutoRandomSeed = true;
		uint32_t randomSeed = 0;
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
		// 保存用Worldへ設定の所有だけを複製する
		static void CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld,
			const Entity& entity, const ParticleSystemComponent& source, ParticleSystemComponent& destination);
	};
	static_assert(sizeof(ParticleSystemComponent) <= 64);
	static_assert(sizeof(ParticleSystemRuntimeComponent) == sizeof(ParticleSystemRuntimeHandle));

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

	// GPUでは数frame遅れた確定値を返す
	bool HasParticleSystemStatistics(const ECSWorld& world, const Entity& entity);
	int32_t GetParticleSystemTrailCount(const ECSWorld& world, const Entity& entity);
	int32_t GetParticleSystemDroppedTrailCount(const ECSWorld& world, const Entity& entity);

	// json変換
	void from_json(const nlohmann::json& in, ParticleSystemComponent& component);
	void to_json(nlohmann::json& out, const ParticleSystemComponent& component);

} // Engine
