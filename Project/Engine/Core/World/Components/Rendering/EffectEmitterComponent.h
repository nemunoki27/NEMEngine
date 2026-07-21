#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleTypes.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	EffectEmitterComponent enum class
	//============================================================================
	// エフェクト1つ分の発生方式
	enum class EffectEmitterMode :
		uint8_t {

		Once,
		Continuous,
		Count,
	};

	// 実行要求の種類
	enum class EffectEmitterCommandType :
		uint8_t {

		Emit,
		StopHandle,
		StopGroup,
		StopAll,
		ClearHandle,
		ClearGroup,
		ClearAll,
	};

	//============================================================================
	//	EffectEmitterComponent struct
	//============================================================================
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

	// グループ内で発生するエフェクト1つ分の設定
	struct EffectEmitterState {

		UUID id = UUID::New();
		std::string name = "Effect";
		bool enabled = true;
		AssetID effect{};
		EffectEmitterMode mode = EffectEmitterMode::Continuous;
		float delay = 0.0f;
		int32_t count = 1;
		float interval = 0.0f;
		float duration = 0.0f;
		Vector3 localPosition = Vector3::AnyInit(0.0f);
		Quaternion localRotation = Quaternion::Identity();
		Vector3 localScale = Vector3::AnyInit(1.0f);
	};

	// 同時に発生するエフェクト設定の束
	struct EffectEmitterGroup {

		std::string name = "Default";
		std::vector<EffectEmitterState> states{ EffectEmitterState{} };
	};

	// ParticleEffectアセット1つ分の実行状態
	struct ParticleEffectInstanceRuntime {

		uint64_t id = 0;
		AssetID effect{};
		bool oneShot = false;
		bool emissionStopped = false;
		float runtimeGroupEmitTimer = 0.0f;
		bool runtimeGroupEmitted = false;
		ParticleEffectGroupEmissionMode runtimeGroupEmissionMode = ParticleEffectGroupEmissionMode::Independent;
		PrimitiveRenderSpace runtimeSpace = PrimitiveRenderSpace::World3D;
		AssetID runtimeEffectID{};
		uint64_t runtimeEffectRevision = 0;
		std::vector<ParticleGroupRuntimeState> runtimeGroups{};
	};

	// EffectEmitterState1つ分の実行状態
	struct EffectEmitterStateRuntime {

		EffectEmitterState state{};
		float time = 0.0f;
		float emitTimer = 0.0f;
		int32_t emittedCount = 0;
		bool scheduleFinished = false;
		std::vector<ParticleEffectInstanceRuntime> effects{};
	};

	// Emit1回分の実行状態
	struct EffectEmitterPlaybackRuntime {

		uint64_t id = 0;
		std::string groupName{};
		bool fixedAnchor = false;
		Vector3 fixedPosition = Vector3::AnyInit(0.0f);
		Quaternion fixedRotation = Quaternion::Identity();
		bool stopped = false;
		std::vector<EffectEmitterStateRuntime> states{};
	};

	// C#とエディターから受け取る実行要求
	struct EffectEmitterCommand {

		EffectEmitterCommandType type = EffectEmitterCommandType::Emit;
		uint64_t playbackID = 0;
		std::string groupName{};
		bool fixedAnchor = false;
		Vector3 position = Vector3::AnyInit(0.0f);
		Quaternion rotation = Quaternion::Identity();
	};

	// 複数のエフェクトグループを名前付きで再生する
	struct EffectEmitterComponent {

		bool enabled = true;
		std::vector<EffectEmitterGroup> groups{ EffectEmitterGroup{} };
		std::string defaultGroup = "Default";
		bool playOnStart = true;
		bool playInEditMode = true;
		bool drawEmitterShape = false;

		int32_t layer = 0;
		int32_t order = 0;
		bool visible = true;

		bool runtimeStarted = false;
		uint64_t runtimeNextPlaybackID = 1;
		uint64_t runtimeNextEffectInstanceID = 1;
		std::vector<EffectEmitterPlaybackRuntime> runtimePlaybacks{};
		std::vector<EffectEmitterCommand> runtimeCommands{};

		uint64_t Emit(std::string_view groupName = {});
		uint64_t EmitAt(std::string_view groupName, const Vector3& position, const Quaternion& rotation);
		void Stop(uint64_t playbackID);
		void Stop(std::string_view groupName);
		void Stop();
		void Clear(uint64_t playbackID);
		void Clear(std::string_view groupName);
		void Clear();
		bool IsPlaying(uint64_t playbackID) const;
		bool IsPlaying(std::string_view groupName) const;
	};

	// json変換
	void from_json(const nlohmann::json& in, EffectEmitterComponent& component);
	void to_json(nlohmann::json& out, const EffectEmitterComponent& component);

	ENGINE_REGISTER_COMPONENT(EffectEmitterComponent, "EffectEmitter");
	ENGINE_REGISTER_COMPONENT_ALIAS(EffectEmitterComponent, "ParticleEmitter");
} // Engine
