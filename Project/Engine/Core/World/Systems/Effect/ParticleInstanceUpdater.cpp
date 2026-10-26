#include "ParticleInstanceUpdater.h"

//============================================================================
//	include
//============================================================================
#include "ParticleParenting.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include "ParticleModuleExecution.h"
#include "ParticleGPURequests.h"
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Foundation/Utility/Random/RandomGeneratorScope.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmissionClock.h>
#include <Engine/Core/Rendering/Particle/ParticleSimulationValidation.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// c++
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <exception>

using namespace Engine::ParticleParenting;
using namespace Engine::ParticleModuleExecution;

bool Engine::ParticleInstanceUpdater::UpdateInstance(ECSWorld& world,
	ParticleInstanceState& instance, std::shared_ptr<const ParticleSystemSettings> settings,
	const Matrix4x4& emitterWorld, const ParticlePhaseParentSettings& parentSettings,
	bool usePhaseParentSettings, float deltaTime, bool updateSimulation,
	bool emissionEnabled, bool drawEmitterShape, ParticleSimulationTarget target) {

	if (instance.target != target) {
		instance.target = target;
		instance.definition.reset();
		instance.rejectedSettings.reset();
		instance.rejectedReason.clear();
		instance.emitter = ParticleEmitterRuntime{};
	}

	// 設定を完成させてから公開し、入力失敗時は現在の状態を保つ
	if ((!instance.definition || instance.definition->settings != settings) && instance.rejectedSettings != settings) {
		try {
			const auto reason = ValidateParticleSimulationSettings(*settings, target);
			if (!reason.empty()) throw std::invalid_argument(reason);
			auto replacement = definitions_.Resolve(settings);
			if (!instance.definition || instance.definition->structure != replacement->structure) {
				instance.emitter = ParticleEmitterRuntime{};
				instance.emitter.particles.Configure(replacement->stateCount);
				instance.emitter.emitTimer = (std::max)(0.0f, settings->emitter.emitInterval);
				instance.randomInitialized = false;
			}
			instance.definition = std::move(replacement);
			instance.emitter.renderSettings = instance.definition->renderSettings;
			instance.rejectedSettings.reset();
			instance.rejectedReason.clear();
			world.MarkRenderDataModified();
		} catch (const std::exception& error) {
			instance.rejectedSettings = std::move(settings);
			instance.rejectedReason = error.what();
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ParticleSystemの設定を反映できません 内容={}", error.what());
		}
	}
	if (!instance.definition) return false;

	// 再生開始時だけSeedを確定する
	if (!instance.randomInitialized) {
		if (instance.useAutoRandomSeed) instance.randomSeed = std::random_device{}();
		instance.randomGenerator.seed(instance.randomSeed);
		instance.randomInitialized = true;
	}
	RandomGeneratorScope randomScope(instance.randomGenerator);
	if (target == ParticleSimulationTarget::GPU) {
		if (drawEmitterShape) DrawEmitterShape(emitterWorld, instance.definition->settings->emitter,
			instance.definition->settings->space == PrimitiveRenderSpace::Screen2D);
		return QueueParticleGPUUpdate(instance, emitterWorld, parentSettings, usePhaseParentSettings,
			deltaTime, updateSimulation, emissionEnabled);
	}
	const auto& definition = *instance.definition;
	UpdateEmitter(emitterWorld, instance.emitter, *definition.settings, definition,
		parentSettings, usePhaseParentSettings, deltaTime, updateSimulation,
		emissionEnabled, instance.oneShot, drawEmitterShape);

	// 放出停止後も既存の粒子とTrailが消えるまで保持する
	if (!instance.emitter.particles.empty() || !instance.emitter.trails.empty()) return false;
	if (instance.emissionStopped) return true;
	return (instance.oneShot || !definition.settings->looping) && instance.emitter.emitted;
}

void Engine::ParticleInstanceUpdater::UpdateEmitter(const Matrix4x4& emitterWorld,
	ParticleEmitterRuntime& state, const ParticleSystemSettings& group,
	const ParticleSystemDefinition& runtime,
	const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
	float deltaTime, bool updateSimulation, bool emissionEnabled,
	bool oneShot, bool drawEmitterShape) {

	parentRuntimes_.assign(runtime.phases.size(), ParticleParentPose{});
	ResolveParticleParents(emitterWorld, runtime,
		parentSettings, useAssetParentSettings, parentRuntimes_);
	const std::vector<ParticleParentPose>& parents = parentRuntimes_;
	if (!updateSimulation) {

		UpdateParticleParents(state.particles, runtime,
			parentSettings, useAssetParentSettings, parents);
		// 寿命を進めず、移動した親へTrailの先端を合わせる
		if (group.trail.enabled) trails_.RecordTrails(state, group.trail, 0.0f);
		if (drawEmitterShape) {
			DrawEmitterShape(emitterWorld, group.emitter, group.space == PrimitiveRenderSpace::Screen2D);
		}
		return;
	}

	const bool emitOnce = oneShot || !group.looping;
	const bool emitAllowed = emissionEnabled && !(emitOnce && state.emitted);

	// 寿命と移動、終端はLifeEndModeに従って遷移し、破棄する粒子は末尾と入れ替える
	ParticleStorage& particles = state.particles;
	particles.Reserve(group.emitter.maxParticles);
	const bool hasUpdateBatch = runtime.hasUpdateBatch;
	for (size_t i = 0; i < particles.size();) {

		Particle particle = particles.GetMutable(i);
		if (particle.phaseIndex < runtime.phases.size()) {
			const ParticlePhaseDefinition& phase = runtime.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		}
		const uint32_t previousPhase = particle.phaseIndex;
		particle.previousAge = particle.age;
		particle.previousPhaseIndex = particle.phaseIndex;
		particle.age += deltaTime;
		if (particle.lifetime <= particle.age && !AdvancePhaseOnLifeEnd(particle, runtime.phases)) {

			particles.RemoveSwap(i);
			continue;
		}
		if (particle.phaseIndex != previousPhase && particle.phaseIndex < runtime.phases.size()) {
			const ParticlePhaseDefinition& phase = runtime.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
			// 遷移先の初期値を適用し、指定のない形状は保持する
			std::fill(particle.rotationSpeeds.begin(), particle.rotationSpeeds.end(), Vector3::AnyInit(0.0f));
			ExecuteSpawnModules(std::span<Particle>(&particle, 1), phase);
			particle.previousPhaseIndex = particle.phaseIndex;
			particle.previousAge = 0.0f;
		}
		particle.pos += particle.velocity * deltaTime;
		if (!hasUpdateBatch) {

			if (particle.phaseIndex < runtime.phases.size()) {
				ApplyUpdateModules(particle, runtime.phases[particle.phaseIndex], deltaTime);
			}
			const ParticleParentPose* parent = particle.phaseIndex < parents.size() ?
				&parents[particle.phaseIndex] : nullptr;
			RefreshParticleWorldTransform(particle, parent);
		}
		particles.Set(i, particle);
		++i;
	}

	// Batchを含む場合だけフェーズ別の連続範囲を作って登録順に実行する
	if (hasUpdateBatch) {

		for (uint32_t phase = 0; phase < runtime.phases.size(); ++phase) {
			particleScratch_.clear();
			particleIndices_.clear();
			for (size_t i = 0; i < particles.size(); ++i) {
				Particle particle = particles.GetMutable(i);
				if (particle.phaseIndex != phase) continue;
				particleIndices_.push_back(i);
				particleScratch_.push_back(particle);
			}
			ExecuteUpdateModules(particleScratch_, runtime.phases[phase], deltaTime);
			for (size_t i = 0; i < particleScratch_.size(); ++i) {
				RefreshParticleWorldTransform(particleScratch_[i], &parents[phase]);
				particles.Set(particleIndices_[i], particleScratch_[i]);
			}
		}
	}

	// 発生間隔ごとに発生させ、上限でクランプする
	if (emitAllowed) {

		const ParticleEmitterSettings& emitterSettings = group.emitter;
		const uint32_t capacity = particles.size() < emitterSettings.maxParticles ?
			emitterSettings.maxParticles - static_cast<uint32_t>(particles.size()) : 0;
		uint32_t emitCount = emitOnce ? 1u :
			AdvanceParticleEmissionClock(state.emitTimer, emitterSettings.emitInterval, deltaTime);
		state.emitted |= emitCount != 0;
		uint32_t spawnCount = 0;
		// 大量の追いつき発生でも上限を超えて抽選しない
		if (emitterSettings.emitCount.type == ParticleValueType::Constant) {
			spawnCount = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(capacity),
				static_cast<uint64_t>(emitCount) * emitterSettings.emitCount.constant));
		} else {
			emitCount = (std::min)(emitCount, capacity);
			for (uint32_t i = 0; i < emitCount && spawnCount < capacity; ++i) {
				spawnCount += (std::min)(emitterSettings.emitCount.Sample(), capacity - spawnCount);
			}
		}
		// 全面無効の形状からは粒子を発生させない
		const auto* emitterShape = ParticleEmitterShapeRegistry::GetInstance().Find(emitterSettings.shape);
		if (emitterShape && !emitterShape->CanEmit(emitterSettings)) {
			spawnCount = 0;
		}
		if (0 < spawnCount && !runtime.phases.empty()) {

			const size_t firstIndex = particles.size();
			particleScratch_.assign(spawnCount, Particle{});
			for (uint32_t i = 0; i < spawnCount; ++i) particles.Append(Particle{});
			for (uint32_t i = 0; i < spawnCount; ++i)
				particleScratch_[i].rotationSpeeds = particles.GetMutable(firstIndex + i).rotationSpeeds;
			std::span<Particle> newborn(particleScratch_);
			// エミッター形状から初期状態を決めてから、先頭フェーズのモジュールの発生処理を通す
			const ParticlePhaseDefinition& firstPhase = runtime.phases.front();
			InitEmitterParticles(newborn, emitterSettings, firstPhase.lifetime,
				group.space == PrimitiveRenderSpace::Screen2D, state.nextParticleID);
			state.nextParticleID += spawnCount;
			// 最初のPhaseは基本形状、以降は直前の評価値を保持する
			const ParticleShapeData initialShape = MakeParticleShapeData(state.renderSettings);
			for (Particle& particle : newborn) {
				particle.shapeData = initialShape;
			}

			// エミッターのワールド行列で発生位置と速度を変換する
			const bool hasSpawnBatch = firstPhase.hasSpawnBatch || firstPhase.hasUpdateBatch;
			if (hasSpawnBatch) {

				ExecuteSpawnModules(newborn, firstPhase);
			}
			for (Particle& particle : newborn) {

				if (!hasSpawnBatch) { ApplySpawnModules(particle, firstPhase); }
				const Vector3 worldPos = Vector3::Transform(particle.pos, emitterWorld);
				particle.velocity = Vector3::Transform(particle.pos + particle.velocity, emitterWorld) - worldPos;
				particle.spawnDirection = Vector3::NormalizeOr(
					Vector3::Transform(particle.pos + particle.spawnDirection, emitterWorld) - worldPos,
						Vector3(0.0f, 1.0f, 0.0f));
				particle.pos = worldPos;
				// 発生時の回転とスケールは親ローカル値として継承する
				UpdateParticleParent(particle, ResolveParticleParentSettings(
					firstPhase, parentSettings, useAssetParentSettings), parents.front(), false);
				if (!hasSpawnBatch) {

					// 発生した瞬間の見た目を確定させる
					ApplyUpdateModules(particle, firstPhase, 0.0f);
					RefreshParticleWorldTransform(particle, &parents.front());
				}
			}
			if (hasSpawnBatch) {

				ExecuteUpdateModules(newborn, firstPhase, 0.0f);
				for (Particle& particle : newborn) {
					RefreshParticleWorldTransform(particle, &parents.front());
				}
			}
			for (size_t i = 0; i < newborn.size(); ++i) particles.Set(firstIndex + i, newborn[i]);
		}
	}

	// トレイルの軌跡点をワールド空間で記録する
	if (group.trail.enabled) {
		trails_.RecordTrails(state, group.trail, deltaTime);
	} else if (!state.trails.empty()) {
		state.trails.clear();
	}

	// エミッター形状のデバッグ描画
	if (drawEmitterShape) {
		DrawEmitterShape(emitterWorld, group.emitter, group.space == PrimitiveRenderSpace::Screen2D);
	}
}

void Engine::ParticleInstanceUpdater::InitEmitterParticles(std::span<Particle> newborn,
	const ParticleEmitterSettings& settings, const ParticleValue<float>& lifetime,
	bool is2D, uint64_t firstSpawnIndex) const {

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);

	ParticleSpawnIndex spawnIndex{};
	spawnIndex.global = static_cast<uint32_t>(firstSpawnIndex);
	spawnIndex.batchCount = static_cast<uint32_t>(newborn.size());
	for (Particle& particle : newborn) {

		Vector3 position = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);
		if (shape) {
			shape->InitParticle(position, direction, settings, is2D, spawnIndex);
		}
		// 配置とModuleで同じ発生番号を使う
		particle.id = firstSpawnIndex++;
		++spawnIndex.global;
		++spawnIndex.batchIndex;

		// 発生座標にオフセットを掛ける
		particle.pos = position + settings.emitOffset.Sample();
		particle.spawnDirection = Vector3::NormalizeOr(direction, Vector3(0.0f, 1.0f, 0.0f));
		particle.velocity = direction * settings.speed.Sample();
		particle.lifetime = (std::max)(lifetime.Sample(), 0.001f);
	}
}

void Engine::ParticleInstanceUpdater::DrawEmitterShape(const Matrix4x4& emitterWorld,
	const ParticleEmitterSettings& settings, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);
	if (!shape) {
		return;
	}

	LineRenderer3D* renderer = nullptr;
	if (!is2D) {
		renderer = LineRenderer::GetInstance()->Get3D();
		if (renderer) {
			renderer->SetOccludedMode(true);
		}
	}
	ParticleEmitterShapeRegistry::GetInstance().DrawDebugShape(settings, emitterWorld, is2D);
	if (renderer) {
		renderer->SetOccludedMode(false);
	}
#endif
}
