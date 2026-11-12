#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Effect/ParticleSimulationReplay.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiator.h>

// c++
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace Engine;

namespace {

	struct ParticleOwner {

		TimelineControlOwner control;
		bool retired = false;
	};
	struct ParticleLease {

		Entity target{};
		ParticleSystemRuntimeHandle handle{};
		Engine::UUID playingInstance{};
		std::vector<ParticleOwner> owners;
		std::optional<ParticleSystemRuntimeData> previous;
	};
	struct ParticleLeaseStorage {

		std::vector<std::shared_ptr<ParticleLease>> values;
	};

	constexpr std::string_view kParticleSamplingDiagnostic =
		"GPU粒子の外部入力は現在のシーンから近似します。シーク中のイベント、塗装、衝突デカールは生成しません";

	class ParticleClip final : public ITimelineClipInstance {
	public:

		explicit ParticleClip(const TimelineClip& clip) : seed_(clip.data.value("randomSeed", 1u)),
			oneShot_(clip.data.value("oneShot", false)), stopOnEnd_(clip.data.value("stopEmittingOnEnd", true)),
			clearOnCancel_(clip.data.value("clearOnCancel", true)) {}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (!context.world.TryGetComponentForBinding<ParticleSystemComponent>(context.target) ||
				!TryGetParticleSystemRuntime(context.world, context.target)) {
				diagnostic = "対象にParticleSystemを追加してください"; return false;
			}
			AcquireTimelineControl(context.world, context.target, "ParticleSystem", {}, context.control);
			auto& leases = context.world.GetStorage().Get<ParticleLeaseStorage>().values;
			for (const auto& lease : leases) {
				if (lease->target != context.target) continue;
				if (std::any_of(lease->owners.begin(), lease->owners.end(), [&](const auto& owner) {
					return owner.control.player == context.control.player && !owner.retired;
				})) {
					diagnostic = "同じParticleSystemへParticleクリップを重ねられません。別の対象かPrefabクリップを使用してください";
					return false;
				}
				lease_ = lease;
				break;
			}
			if (!lease_) {
				lease_ = std::make_shared<ParticleLease>();
				lease_->target = context.target;
				lease_->handle = context.world.GetComponent<ParticleSystemRuntimeComponent>(context.target).handle;
				if (context.mode == TimelineEvaluationMode::Preview)
					lease_->previous = std::move(*TryGetParticleSystemRuntime(context.world, context.target));
				leases.push_back(lease_);
			}
			lease_->owners.push_back({context.control, false});
			if (OwnsTimelineControl(context.world, context.target, "ParticleSystem", {}, context.control)) TakeOwnership(context);
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (!OwnsTimelineControl(context.world, context.target, "ParticleSystem", {}, context.control)) return;
			const auto* storage = context.world.TryGetComponentForBinding<ParticleSystemRuntimeComponent>(context.target);
			if (!storage || !lease_ || storage->handle != lease_->handle)
				throw std::invalid_argument("再生対象のParticleSystemが削除または再作成されました");
			if (!lease_ || lease_->playingInstance != context.control.instance) TakeOwnership(context);
			auto* component = context.world.TryGetComponentForBinding<ParticleSystemComponent>(context.target);
			auto* runtime = TryGetParticleSystemRuntime(context.world, context.target);
			if (!component || !runtime) return;
			ParticlePhaseParentSettings parent;
			const bool phaseParent = component->simulationSpace == ParticleSystemSimulationSpace::PhaseSettings;
			parent.useEmitter = component->simulationSpace == ParticleSystemSimulationSpace::Local;
			const auto settings = GetParticleSystemSettings(context.world, context.target);
			if (!settings) return;
			const auto sample = [&](double sourceTime, Matrix4x4& result) {
				if (context.sampleTargetTransform) {
					const double time = context.clip.speed > 0.000001 ?
						context.clip.start + (sourceTime - context.clip.clipIn) / context.clip.speed : context.clip.start;
					return context.sampleTargetTransform((std::max)(0.0, time), context.target, result);
				}
				ResolvedWorldTransform transform;
				if (!TransformWorldUtility::ResolveWorldTransform(context.world, context.target, transform, true)) return false;
				result = transform.matrix;
				return true;
			};
			replay_.Evaluate(context.world, runtime->instance, settings, component->simulationTarget,
				parent, phaseParent, (std::max)(0.0, context.localTime), context.samplingRevision,
				context.isScrubbing, sample);
			const auto& gpu = runtime->instance.emitter.gpu;
			if (context.output && context.output->diagnostic == kParticleSamplingDiagnostic) context.output->diagnostic.clear();
			if (context.isScrubbing && gpu && context.output &&
				(gpu->program->usesWorldInputs || settings->source.kind != ParticleSourceKind::Analytic ||
				!settings->feedback.events.empty() || settings->feedback.paint.enabled || settings->feedback.decal.enabled))
				context.output->diagnostic = kParticleSamplingDiagnostic;
			runtime->playing = true;
			runtime->paused = context.paused;
			runtime->stopped = false;
		}

		void Retire(TimelineEvaluationContext& context) override {

			if (!lease_) return;
			replay_.Reset(seed_, oneShot_);
			for (auto& owner : lease_->owners)
				if (owner.control.instance == context.control.instance) owner.retired = true;
		}

		void Exit(TimelineEvaluationContext& context, TimelineExitReason reason) override {

			if (!lease_) return;
			replay_.Reset(seed_, oneShot_);
			std::erase_if(lease_->owners, [&](const auto& owner) { return owner.control.instance == context.control.instance; });
			if (lease_->playingInstance == context.control.instance) lease_->playingInstance = {};
			if (!lease_->owners.empty()) { lease_.reset(); return; }
			auto& leases = context.world.GetStorage().Get<ParticleLeaseStorage>().values;
			std::erase(leases, lease_);
			const auto* storage = context.world.TryGetComponentForBinding<ParticleSystemRuntimeComponent>(context.target);
			if (!storage || storage->handle != lease_->handle) { lease_.reset(); return; }
			auto* runtime = TryGetParticleSystemRuntime(context.world, context.target);
			if (!runtime) { lease_.reset(); return; }
			if (runtime->instance.emitter.gpu) {
				runtime->instance.emitter.gpu->deterministicReplay = false;
				runtime->instance.emitter.gpu->suppressFeedback = false;
			}
			if (lease_->previous) { *runtime = std::move(*lease_->previous); lease_.reset(); return; }
			const bool clear = reason != TimelineExitReason::Completed && clearOnCancel_;
			if (clear) { *runtime = ParticleSystemRuntimeData{}; runtime->initialized = true; }
			else if (stopOnEnd_) { runtime->instance.emissionStopped = true; runtime->playing = true; runtime->paused = false; }
			lease_.reset();
			context.world.MarkRenderDataModified();
		}
		float GetPreparationProgress() const override {

			return 1.0f;
		}

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip,
			[[maybe_unused]] std::string& diagnostic) override {

			const auto seed = clip.data.value("randomSeed", 1u);
			const auto oneShot = clip.data.value("oneShot", false);
			const bool restart = seed != seed_ || oneShot != oneShot_;
			seed_ = seed;
			oneShot_ = oneShot;
			stopOnEnd_ = clip.data.value("stopEmittingOnEnd", true);
			clearOnCancel_ = clip.data.value("clearOnCancel", true);
			if (restart) {
				if (auto* runtime = TryGetParticleSystemRuntime(context.world, context.target)) Reset(*runtime);
			}
			return true;
		}
	private:

		uint32_t seed_ = 1;
		bool oneShot_ = false, stopOnEnd_ = true, clearOnCancel_ = true;
		ParticleSimulationReplay replay_;
		std::shared_ptr<ParticleLease> lease_;

		// 別Playerから制御を引き継いだ場合も絶対時刻から再現する
		void TakeOwnership(TimelineEvaluationContext& context) {

			if (!lease_) return;
			if (auto* runtime = TryGetParticleSystemRuntime(context.world, context.target)) Reset(*runtime);
			lease_->playingInstance = context.control.instance;
		}

		// 乱数列も含めて同じ開始状態へ戻す
		void Reset(ParticleSystemRuntimeData& runtime) {

			ResetParticleSimulation(runtime.instance, seed_, oneShot_);
			runtime.initialized = true;
			runtime.playing = true;
			runtime.paused = false;
			runtime.stopped = false;
			runtime.commands.clear();
			replay_.Reset(seed_, oneShot_);
		}
	};

	class PrefabClip final : public ITimelineClipInstance {
	public:

		explicit PrefabClip(const TimelineClip& clip) : asset_(ParseAssetID(clip.data, "asset")),
			parentBinding_(clip.data.value("parentBinding", "")), joint_(clip.data.value("joint", "")),
			destroyOnEnd_(clip.data.value("destroyOnEnd", true)) {

			if (clip.data.contains("offset")) offset_ = Vector3::FromJson(clip.data.at("offset"));
			if (clip.data.contains("rotation")) rotation_ = Vector3::FromJson(clip.data.at("rotation"));
			if (clip.data.contains("scale")) scale_ = Vector3::FromJson(clip.data.at("scale"));
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (attempted_) {
				diagnostic = failure_;
				return failure_.empty();
			}
			attempted_ = true;
			const auto lifetime = context.world.GetLifetime();
			try {
				if (TryEnter(context, diagnostic)) return true;
			} catch (const std::exception& error) {
				if (!lifetime->IsAlive()) throw;
				diagnostic = error.what();
			}
			failure_ = diagnostic.empty() ? "Prefabを生成できません" : diagnostic;
			diagnostic = failure_;
			return false;
		}

		void Evaluate([[maybe_unused]] TimelineEvaluationContext& context) override {}

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip,
			[[maybe_unused]] std::string& diagnostic) override {

			PrefabClip candidate(clip);
			if (asset_ != candidate.asset_ || parentBinding_ != candidate.parentBinding_ || joint_ != candidate.joint_)
				return false;
			offset_ = candidate.offset_;
			rotation_ = candidate.rotation_;
			scale_ = candidate.scale_;
			destroyOnEnd_ = candidate.destroyOnEnd_;
			if (auto* transform = context.world.TryGetComponentForBinding<TransformComponent>(instance_.root)) {
				transform->localPos = offset_;
				transform->localRotation = Quaternion::FromEulerDegrees(rotation_);
				transform->localScale = scale_;
				MarkTransformSubtreeDirty(context.world, instance_.root);
			}
			return true;
		}

		void Exit(TimelineEvaluationContext& context, TimelineExitReason reason) override {

			if (!destroyOnEnd_ && reason == TimelineExitReason::Completed && context.mode != TimelineEvaluationMode::Preview) return;
			for (auto it = instance_.createdEntities.rbegin(); it != instance_.createdEntities.rend(); ++it)
				if (context.world.IsAlive(*it)) context.world.DestroyEntity(*it);
			instance_ = {};
		}
	private:

		AssetID asset_{};
		std::string parentBinding_, joint_, failure_;
		Vector3 offset_{}, rotation_{}, scale_{1, 1, 1};
		bool destroyOnEnd_ = true, attempted_ = false;
		PrefabInstantiateResult instance_;

		// 読込失敗は同じ再生Instance内で再試行しない
		bool TryEnter(TimelineEvaluationContext& context, std::string& diagnostic) {

			if (!asset_ || !context.systems.assetDatabase) { diagnostic = "Prefabアセットを設定してください"; return false; }
			const auto parent = parentBinding_.empty() ? context.target : context.ResolveBinding(parentBinding_);
			if (!parentBinding_.empty() && !context.world.IsAlive(parent)) {
				diagnostic = "Prefabの親を割り当ててください"; return false;
			}
			if (!joint_.empty() && !context.world.TryGetComponentForBinding<SkinnedAnimationComponent>(parent)) {
				diagnostic = "ジョイント接続先にSkinnedAnimationが必要です"; return false;
			}
			PrefabInstantiateDesc desc;
			desc.parent = parent;
			if (const auto* scene = context.world.TryGetComponentForBinding<SceneObjectComponent>(parent))
				desc.ownerSceneInstanceID = scene->sceneInstanceID;
			HierarchySystem hierarchy;
			if (!PrefabInstantiator::InstantiatePrefab(*context.systems.assetDatabase, hierarchy, context.world, asset_, instance_, desc)) {
				diagnostic = "Prefabを生成できません"; return false;
			}
			if (!context.world.IsAlive(instance_.root)) { diagnostic = "Prefabに生成可能なルートがありません"; return false; }
			if (auto* transform = context.world.TryGetComponentForBinding<TransformComponent>(instance_.root)) {
				transform->localPos = offset_;
				transform->localRotation = Quaternion::FromEulerDegrees(rotation_);
				transform->localScale = scale_;
				MarkTransformSubtreeDirty(context.world, instance_.root);
			}
			if (!joint_.empty()) {
				const auto* scene = context.world.TryGetComponentForBinding<SceneObjectComponent>(parent);
				if (scene) {
					const Engine::UUID parentLocalFileID = scene->localFileID;
					if (!context.world.TryGetComponentForBinding<JointAttachmentComponent>(instance_.root))
						context.world.AddComponent<JointAttachmentComponent>(instance_.root);
					if (auto* attachment = context.world.TryGetComponentForBinding<JointAttachmentComponent>(instance_.root)) {
						attachment->skinnedEntityLocalFileID = parentLocalFileID;
						attachment->jointName = joint_;
					}
				}
			}
			return true;
		}

	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineEffectClip(TimelineTrackKind kind, const TimelineClip& clip) {

	if (kind == TimelineTrackKind::Particle) return std::make_unique<ParticleClip>(clip);
	return std::make_unique<PrefabClip>(clip);
}
