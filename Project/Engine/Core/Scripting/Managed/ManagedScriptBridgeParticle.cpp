#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace Engine {

	namespace {

		enum class ParticleSystemOperation :
			int32_t {

			Play,
			Pause,
			Stop,
			Clear,
			PlayOneShot,
		};

		enum class ParticleSystemStateQuery :
			int32_t {

			Playing,
			Emitting,
			Paused,
			Stopped,
			Alive,
			ParticleCount,
		};

		template<typename Function>
		void ForEachParticleSystem(ECSWorld& world, const Entity& root,
			bool withChildren, Function&& function) {

			if (!withChildren) {
				if (world.HasComponent<ParticleSystemComponent>(root)) {
					function(root);
				}
				return;
			}

			for (const Entity& entity :
				HierarchyUtility::CollectLogicalSubtree(world, root)) {

				if (world.HasComponent<ParticleSystemComponent>(entity)) {
					function(entity);
				}
			}
		}

		bool QueryParticleSystemState(const ECSWorld& world,
			const Entity& entity, ParticleSystemStateQuery state) {

			switch (state) {
			case ParticleSystemStateQuery::Playing:
				return IsParticleSystemPlaying(world, entity);
			case ParticleSystemStateQuery::Emitting:
				return IsParticleSystemEmitting(world, entity);
			case ParticleSystemStateQuery::Paused:
				return IsParticleSystemPaused(world, entity);
			case ParticleSystemStateQuery::Stopped:
				return IsParticleSystemStopped(world, entity);
			case ParticleSystemStateQuery::Alive:
				return IsParticleSystemAlive(world, entity);
			case ParticleSystemStateQuery::ParticleCount:
				return 0 < GetParticleSystemParticleCount(world, entity);
			}
			return false;
		}
	}
	void ManagedScriptRuntime::ParticleSystemControlCallback(
		ManagedNativeEntity entity, int32_t operation,
		int32_t stopBehavior, int32_t withChildren) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity root = ResolveEntity(entity);
		if (!world || !world->IsAlive(root) ||
			operation < static_cast<int32_t>(ParticleSystemOperation::Play) ||
			static_cast<int32_t>(ParticleSystemOperation::PlayOneShot) < operation) {
			return;
		}

		const ParticleSystemOperation command =
			static_cast<ParticleSystemOperation>(operation);
		ForEachParticleSystem(*world, root, withChildren != 0,
			[&](const Entity& target) {

				switch (command) {
				case ParticleSystemOperation::Play:
					RequestParticleSystemPlay(*world, target);
					break;
				case ParticleSystemOperation::Pause:
					RequestParticleSystemPause(*world, target);
					break;
				case ParticleSystemOperation::Stop: {

					const ParticleSystemStopBehavior behavior = stopBehavior == 0 ?
						ParticleSystemStopBehavior::StopEmittingAndClear :
						ParticleSystemStopBehavior::StopEmitting;
					RequestParticleSystemStop(*world, target, behavior);
					break;
				}
				case ParticleSystemOperation::Clear:
					RequestParticleSystemClear(*world, target);
					break;
				case ParticleSystemOperation::PlayOneShot:
					RequestParticleSystemRestart(*world, target, true);
					break;
				}
			});
	}

	int32_t ManagedScriptRuntime::ParticleSystemStateCallback(
		ManagedNativeEntity entity, int32_t state, int32_t withChildren) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity root = ResolveEntity(entity);
		if (!world || !world->IsAlive(root) ||
			state < static_cast<int32_t>(ParticleSystemStateQuery::Playing) ||
			static_cast<int32_t>(ParticleSystemStateQuery::ParticleCount) < state) {
			return 0;
		}

		const ParticleSystemStateQuery query =
			static_cast<ParticleSystemStateQuery>(state);
		if (query == ParticleSystemStateQuery::ParticleCount) {

			int32_t particleCount = 0;
			ForEachParticleSystem(*world, root, withChildren != 0,
				[&](const Entity& target) {

					particleCount +=
						GetParticleSystemParticleCount(*world, target);
				});
			return particleCount;
		}

		bool matched = false;
		ForEachParticleSystem(*world, root, withChildren != 0,
			[&](const Entity& target) {

				matched |= QueryParticleSystemState(*world, target, query);
			});
		return matched ? 1 : 0;
	}
}
