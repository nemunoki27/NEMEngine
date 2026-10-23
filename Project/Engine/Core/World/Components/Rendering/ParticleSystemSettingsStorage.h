#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <memory>

namespace Engine {

	// 共有中の設定は書き換えず、新しいSnapshotへ差し替える
	struct ParticleSystemSettingsRecord {

		std::shared_ptr<const ParticleSystemSettings> value;
		UUID owner{};
		uint64_t revision = 1;
	};
	struct ParticleSystemSettingsStorageTag;
	using ParticleSystemSettingsStorage = GenerationalPool<ParticleSystemSettingsRecord, ParticleSystemSettingsStorageTag>;
	using ParticleSystemSettingsHandle = ParticleSystemSettingsStorage::Handle;

	// Worldが所有する編集設定を借用する
	std::shared_ptr<const ParticleSystemSettings> GetParticleSystemSettings(const ECSWorld& world, const Entity& entity);
	// 検証済みの設定を所有元だけへ適用する
	void SetParticleSystemSettings(ECSWorld& world, const Entity& entity, const ParticleSystemSettings& settings);
}
