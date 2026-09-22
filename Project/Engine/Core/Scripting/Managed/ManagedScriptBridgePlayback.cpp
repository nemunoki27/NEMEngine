#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>

namespace Engine {

	namespace {

		// 対象AudioSourceと所有Worldを同時に解決する
		bool ResolveAudioSource(
			ManagedNativeEntity entity, ECSWorld*& outWorld,
			Entity& outEntity) {

			outWorld = ResolveWorld(entity);
			outEntity = ResolveEntity(entity);
			return outWorld && outWorld->IsAlive(outEntity) &&
				outWorld->HasComponent<AudioSourceComponent>(outEntity);
		}
	}
	float ManagedScriptRuntime::GetSkinnedAnimationDurationCallback(ManagedNativeEntity entity, const char* clipName) {

		if (!clipName) {
			return 0.0f;
		}
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveWorld(entity);
		if (!context || !context->skinnedAnimationManager || !world) {
			return 0.0f;
		}
		const Entity resolved = ResolveEntity(entity);
		const MeshRendererComponent* renderer = world->IsAlive(resolved) ?
			world->TryGetComponent<MeshRendererComponent>(resolved) : nullptr;
		if (!renderer || !renderer->mesh) {
			return 0.0f;
		}
		// メッシュのアニメーションセットから指定クリップの合計長を引く
		const SkinnedMeshAnimationSet* animationSet = context->skinnedAnimationManager->Find(renderer->mesh);
		if (!animationSet || !animationSet->valid) {
			return 0.0f;
		}
		const auto it = animationSet->clips.find(clipName);
		return it != animationSet->clips.end() ? it->second.duration : 0.0f;
	}

	void ManagedScriptRuntime::PlaySkinnedAnimationCallback(ManagedNativeEntity entity, const char* clipName) {

		if (!clipName) {
			return;
		}
		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		SkinnedAnimationComponent* anim = world->IsAlive(resolved) ?
			world->TryGetComponent<SkinnedAnimationComponent>(resolved) : nullptr;
		if (!anim) {
			return;
		}
		// 指定クリップへ切り替えて再生する、終了フラグを同フレームで下ろす
		// 実際の遷移や再生時間のリセットはSkinnedAnimationSystemが行う
		anim->clip = clipName;
		anim->enabled = true;
		if (SkinnedAnimationRuntimeData* runtime =
			TryGetSkinnedAnimationRuntime(*world, resolved)) {
			runtime->animationFinished = false;
		}
	}

	int32_t ManagedScriptRuntime::CopySkinnedAnimationCurrentClipCallback(
		ManagedNativeEntity entity, char* buffer, int32_t capacity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const SkinnedAnimationRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetSkinnedAnimationRuntime(*world, resolved) : nullptr;
		return CopyStringToBuffer(
			runtime ? runtime->currentClip : std::string{}, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::GetSkinnedAnimationRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedSkinnedAnimationRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const SkinnedAnimationRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetSkinnedAnimationRuntime(*world, resolved) : nullptr;
		if (!runtime) {
			return 0;
		}

		// 可変長データを跨がせずC#が必要な固定長状態だけを複写する
		outState->currentTime = runtime->time;
		outState->currentDuration = runtime->currentDuration;
		outState->blendTime = runtime->blendTime;
		outState->repeatCount = runtime->repeatCount;
		outState->initialized = runtime->initialized ? 1 : 0;
		outState->finished = runtime->animationFinished ? 1 : 0;
		outState->inTransition = runtime->inTransition ? 1 : 0;
		return 1;
	}

	void ManagedScriptRuntime::AudioPlayCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPlay(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioPlayOneShotCallback(
		ManagedNativeEntity entity, ManagedAssetGUID clipID, float volumeScale) {

		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPlayOneShot(
				*world, resolved, ToAssetID(clipID), volumeScale);
		}
	}

	void ManagedScriptRuntime::AudioPauseCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPause(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioUnPauseCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioUnPause(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioStopCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioStop(*world, resolved);
		}
	}

	int32_t ManagedScriptRuntime::AudioIsPlayingCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		return ResolveAudioSource(entity, world, resolved) &&
			IsAudioSourcePlaying(*world, resolved) ? 1 : 0;
	}
}
