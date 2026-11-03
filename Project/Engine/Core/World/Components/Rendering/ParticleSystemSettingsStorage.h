#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Time/LocalTime.h>

// c++
#include <memory>

namespace Engine {

	// 共有中の設定は書き換えず、新しいSnapshotへ差し替える
	struct ParticleSystemSettingsRecord {

		std::shared_ptr<const ParticleSystemSettings> value;
		UUID owner{};
		uint64_t revision = 1;
		TimeSourceSettings timeSource{};
	};
	struct ParticleSystemSettingsStorageTag;
	using ParticleSystemSettingsStorage = GenerationalPool<ParticleSystemSettingsRecord, ParticleSystemSettingsStorageTag>;
	using ParticleSystemSettingsHandle = ParticleSystemSettingsStorage::Handle;

	// 時間源は所有元の設定Storageに保持し、ECSチャンクを増やさない
	TimeSourceSettings GetParticleSystemTimeSource(const ECSWorld& world, const Entity& entity);
	TimeSourceSettings* TryGetParticleSystemTimeSource(ECSWorld& world, const Entity& entity);
	void SetParticleSystemTimeSource(ECSWorld& world, const Entity& entity, const TimeSourceSettings& source);

	// Worldが所有する編集設定を借用する
	std::shared_ptr<const ParticleSystemSettings> GetParticleSystemSettings(const ECSWorld& world, const Entity& entity);
	// 検証済みの設定を所有元だけへ適用する
	void SetParticleSystemSettings(ECSWorld& world, const Entity& entity, const ParticleSystemSettings& settings);
}
