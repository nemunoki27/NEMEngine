#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// ビルトインモジュールの自己登録をこの翻訳単位で確定させる
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleGravityForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleFlipbookModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleScaleOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleAlphaReferenceModule.h>

// パラメトリック形状の自己登録をこの翻訳単位で確定させる
#include <Engine/Core/Rendering/Particle/Parametric/ParticleRingParametricShape.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleCylinderParametricShape.h>

// 発生形状の自己登録をこの翻訳単位で確定させる
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleSphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleHemisphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleTorusEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCircleEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleConeEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticlePointEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCone2DEmitterShape.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleSystem classMethods
//============================================================================
void Engine::ParticleSystem::Update(ECSWorld& world, SystemContext& context) {

	// ホットリロードの確認は間隔を空けて行う
	reloadCheckTimer_ += context.unscaledDeltaTime;
	const bool checkReload = kReloadCheckInterval <= reloadCheckTimer_;
	if (checkReload) {
		reloadCheckTimer_ = 0.0f;
	}

	world.ForEach<ParticleEmitterComponent>([&](const Entity& entity, ParticleEmitterComponent& emitter) {

		const EffectRuntime* effect = ResolveEffect(context, emitter.effect, checkReload);
		if (!effect) {
			emitter.runtimeParticles.clear();
			emitter.runtimeTrails.clear();
			return;
		}

		const ParticleEffectAsset& asset = effect->asset;

		// アセットの描画設定をコンポーネントへ反映する、描画側はこの値を参照する
		emitter.runtimeRenderSettings = MakeParticleRenderSettings(asset);

		// 更新を行うか、Play中はTimeScale適用済みのdeltaTime、EditのプレビューはTimeScale非適用のリアル時間を使う
		const bool allowTimeAdvance = emitter.playing && (context.mode == WorldMode::Play || emitter.playInEditMode);
		const float deltaTime = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;
		if (!allowTimeAdvance || deltaTime <= 0.0f) {
			return;
		}

		// エミッターの経過時間、ループなら再生時間内へ折り返す
		emitter.runtimeTime += deltaTime;
		bool emitAllowed = true;
		if (asset.looping) {
			if (0.0f < asset.duration) {
				emitter.runtimeTime = std::fmod(emitter.runtimeTime, asset.duration);
			}
		} else if (asset.duration <= emitter.runtimeTime) {
			emitAllowed = false;
		}

		// 寿命と移動、終端はLifeEndModeに従って遷移し、破棄する粒子は末尾と入れ替える
		std::vector<Particle>& particles = emitter.runtimeParticles;
		for (size_t i = 0; i < particles.size();) {

			Particle& particle = particles[i];
			particle.age += deltaTime;
			if (particle.lifetime <= particle.age && !AdvancePhaseOnLifeEnd(particle, effect->phases)) {

				particle = particles.back();
				particles.pop_back();
				continue;
			}
			particle.position += particle.velocity * deltaTime;
			++i;
		}

		// 生存粒子をフェーズごとのモジュールで一括更新する
		UpdatePhaseModules(particles, *effect, deltaTime);

		// 発生間隔ごとに発生させ、上限でクランプする
		if (emitAllowed) {

			const ParticleEmitterSettings& emitterSettings = asset.emitter;
			uint32_t spawnCount = 0;
			emitter.runtimeEmitTimer += deltaTime;
			if (emitterSettings.emitInterval <= emitter.runtimeEmitTimer) {

				emitter.runtimeEmitTimer = 0.0f;
				spawnCount = emitterSettings.emitCount.Sample();
			}
			const uint32_t capacity = static_cast<uint32_t>(
				(std::max)(0, static_cast<int32_t>(emitterSettings.maxParticles) - static_cast<int32_t>(particles.size())));
			spawnCount = (std::min)(spawnCount, capacity);
			if (0 < spawnCount && !effect->phases.empty()) {

				particles.resize(particles.size() + spawnCount);
				std::span<Particle> newborn(particles.data() + particles.size() - spawnCount, spawnCount);
				// エミッター形状から初期状態を決めてから、先頭フェーズのモジュールの発生処理を通す
				const PhaseRuntime& firstPhase = effect->phases.front();
				InitEmitterParticles(newborn, emitterSettings, firstPhase.lifetime,
					asset.space == PrimitiveRenderSpace::Screen2D);
				for (const auto& module : firstPhase.modules) {
					module->OnSpawn(newborn);
				}

				// エミッターのワールド行列で発生位置と速度を変換し、以降はワールド空間でシミュレーションする
				Matrix4x4 emitterWorld = Matrix4x4::Identity();
				if (const auto* transform = world.TryGetComponent<TransformComponent>(entity)) {
					emitterWorld = transform->worldMatrix;
				}
				for (Particle& particle : newborn) {

					const Vector3 worldPos = Vector3::Transform(particle.position, emitterWorld);
					particle.velocity = Vector3::Transform(particle.position + particle.velocity, emitterWorld) - worldPos;
					particle.position = worldPos;
					// トレイル追跡用のIDを割り当てる
					particle.id = emitter.runtimeNextParticleID++;
				}

				// 発生した瞬間の見た目を確定させ、初回描画が未補間の色や大きさになるのを防ぐ
				for (const auto& module : firstPhase.modules) {
					module->OnUpdate(newborn, 0.0f);
				}
			}
		}

		// トレイルの軌跡点をワールド空間で記録する
		if (asset.trail.enabled) {
			RecordTrails(world, entity, emitter, asset.trail);
		} else if (!emitter.runtimeTrails.empty()) {
			emitter.runtimeTrails.clear();
		}

		// エミッター形状のデバッグ描画
		if (emitter.drawEmitterShape) {

			DrawEmitterShape(world, entity, asset.emitter, asset.space == PrimitiveRenderSpace::Screen2D);
		}
		});
}

const Engine::ParticleSystem::EffectRuntime* Engine::ParticleSystem::ResolveEffect(
	SystemContext& context, AssetID effectID, bool checkReload) {

	if (!context.assetDatabase) {
		return nullptr;
	}
	// 空のエフェクトはビルトインの既定エフェクトへ解決する
	if (!effectID) {
		effectID = BuiltinAssets::Effects::DefaultParticle;
	}

	// キャッシュ済みならエディター編集とファイル更新を確認してそのまま返す
	auto found = effectCache_.find(effectID);
	if (found != effectCache_.end()) {

		// エディターの編集内容は保存を待たず即反映する
		ParticleEffectAsset editedAsset{};
		if (ParticleEffectEditBridge::GetInstance().TryConsume(
			effectID, found->second.appliedEditVersion, editedAsset)) {

			found->second.asset = std::move(editedAsset);
			found->second.valid = true;
			BuildPhases(found->second);
		} else if (checkReload && !found->second.path.empty()) {

			std::error_code ec;
			const auto lastWriteTime = std::filesystem::last_write_time(found->second.path, ec);
			if (!ec && found->second.lastWriteTime != lastWriteTime) {

				const uint64_t appliedEditVersion = found->second.appliedEditVersion;
				found->second = LoadEffect(context, effectID);
				found->second.appliedEditVersion = appliedEditVersion;
			}
		}
		return found->second.valid ? &found->second : nullptr;
	}

	auto [it, inserted] = effectCache_.emplace(effectID, LoadEffect(context, effectID));
	return it->second.valid ? &it->second : nullptr;
}

Engine::ParticleSystem::EffectRuntime Engine::ParticleSystem::LoadEffect(
	SystemContext& context, AssetID effectID) const {

	// アセットを読み込みフェーズを構築する、未登録のモジュールは読み飛ばす
	EffectRuntime runtime{};
	runtime.path = context.assetDatabase->ResolveFullPath(effectID);
	if (runtime.path.empty()) {
		return runtime;
	}

	std::error_code ec;
	runtime.lastWriteTime = std::filesystem::last_write_time(runtime.path, ec);

	const nlohmann::json data = JsonAdapter::Load(runtime.path.string(), false);
	if (!FromJson(data, runtime.asset)) {
		return runtime;
	}

	runtime.valid = true;
	BuildPhases(runtime);
	return runtime;
}

bool Engine::ParticleSystem::AdvancePhaseOnLifeEnd(Particle& particle, const std::vector<PhaseRuntime>& phases) const {

	if (phases.size() <= particle.phaseIndex) {
		return false;
	}

	// 寿命が尽きたときの挙動、値の閉じたenumなのでここで一括処理する
	switch (phases[particle.phaseIndex].lifeEndMode) {
	case ParticleLifeEndMode::Advance: {

		// 次フェーズへ遷移する、位置や速度や色は現在値を引き継ぐ
		const uint32_t next = particle.phaseIndex + 1;
		if (phases.size() <= next) {
			return false;
		}
		particle.phaseIndex = next;
		particle.age = 0.0f;
		particle.lifetime = (std::max)(phases[next].lifetime.Sample(), 0.001f);
		return true;
	}
	case ParticleLifeEndMode::Clamp:

		// 進行度1.0の見た目を保持して生存し続ける
		particle.age = particle.lifetime;
		return true;
	case ParticleLifeEndMode::Reset:

		// 同フェーズを最初からやり直す
		particle.age = 0.0f;
		return true;
	case ParticleLifeEndMode::Kill:
	default:
		return false;
	}
}

void Engine::ParticleSystem::UpdatePhaseModules(std::vector<Particle>& particles,
	const EffectRuntime& effect, float deltaTime) const {

	// フェーズ順に並べ、各フェーズのモジュールを連続範囲へ一括適用する
	std::sort(particles.begin(), particles.end(),
		[](const Particle& lhs, const Particle& rhs) { return lhs.phaseIndex < rhs.phaseIndex; });
	size_t begin = 0;
	while (begin < particles.size()) {

		const uint32_t phaseIndex = particles[begin].phaseIndex;
		size_t end = begin;
		while (end < particles.size() && particles[end].phaseIndex == phaseIndex) {
			++end;
		}
		if (phaseIndex < effect.phases.size()) {

			std::span<Particle> range(particles.data() + begin, end - begin);
			for (const auto& module : effect.phases[phaseIndex].modules) {
				module->OnUpdate(range, deltaTime);
			}
		}
		begin = end;
	}
}

void Engine::ParticleSystem::InitEmitterParticles(std::span<Particle> newborn,
	const ParticleEmitterSettings& settings, const ParticleValue<float>& lifetime, bool is2D) const {

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);

	for (Particle& particle : newborn) {

		Vector3 position = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);
		if (shape) {
			shape->InitParticle(position, direction, settings, is2D);
		}

		particle.position = position;
		particle.velocity = direction * settings.speed.Sample();
		particle.lifetime = (std::max)(lifetime.Sample(), 0.001f);
	}
}

void Engine::ParticleSystem::DrawEmitterShape(ECSWorld& world, const Entity& entity,
	const ParticleEmitterSettings& settings, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);
	if (!shape) {
		return;
	}

	// エミッターのワールド位置と回転を取り出す
	Vector3 center = Vector3::AnyInit(0.0f);
	Quaternion rotation = Quaternion::Identity();
	if (const auto* transform = world.TryGetComponent<TransformComponent>(entity)) {

		Vector3 scale{};
		DecomposeAffine3D(transform->worldMatrix, center, rotation, scale);
	}
	shape->DrawShape(settings, center, rotation, is2D);
#endif
}

void Engine::ParticleSystem::RecordTrails([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	ParticleEmitterComponent& emitter, const ParticleTrailSettings& trail) {

	// 死亡した粒子の軌跡を破棄する
	aliveTrailIDs_.clear();
	for (const Particle& particle : emitter.runtimeParticles) {
		aliveTrailIDs_.insert(particle.id);
	}
	for (auto it = emitter.runtimeTrails.begin(); it != emitter.runtimeTrails.end();) {
		it = aliveTrailIDs_.contains(it->first) ? std::next(it) : emitter.runtimeTrails.erase(it);
	}

	// 一定距離を移動した粒子へ軌跡点を追加する、上限を超えたら古い点を捨てる
	const int32_t maxPoints = (std::max)(trail.maxPoints, 2);
	for (const Particle& particle : emitter.runtimeParticles) {

		const Vector3 worldPos = particle.position;
		std::vector<Vector3>& points = emitter.runtimeTrails[particle.id];
		if (points.empty()) {
			points.emplace_back(worldPos);
			continue;
		}
		const Vector3 diff = worldPos - points.back();
		if (trail.minDistance * trail.minDistance <= Vector3::Dot(diff, diff)) {

			points.emplace_back(worldPos);
			if (maxPoints < static_cast<int32_t>(points.size())) {
				points.erase(points.begin());
			}
		}
	}
}

void Engine::ParticleSystem::BuildPhases(EffectRuntime& runtime) const {

	// 未登録のモジュールは読み飛ばす
	runtime.phases.clear();
	runtime.phases.reserve(runtime.asset.phases.size());
	for (const ParticleEffectPhase& phaseDef : runtime.asset.phases) {

		PhaseRuntime phase{};
		phase.lifetime = phaseDef.lifetime;
		phase.lifeEndMode = phaseDef.lifeEndMode;
		for (const ParticleEffectModuleEntry& entry : phaseDef.modules) {

			auto module = ParticleModuleRegistry::GetInstance().Create(entry.id);
			if (!module) {
				continue;
			}
			module->FromJson(entry.params);
			phase.modules.emplace_back(std::move(module));
		}
		runtime.phases.emplace_back(std::move(phase));
	}
}
