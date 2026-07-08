#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// ビルトインモジュールの自己登録をこの翻訳単位で確定させる
#include <Engine/Core/Rendering/Particle/Modules/ParticleSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleRotationOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleGravityForceModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleNoiseForceModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleFlipbookModule.h>
#include <Engine/Core/Rendering/Particle/Modules/ParticleShapeOverLifetimeModule.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

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

		// 寿命と移動、死亡した粒子は末尾と入れ替えて破棄する
		std::vector<Particle>& particles = emitter.runtimeParticles;
		for (size_t i = 0; i < particles.size();) {

			Particle& particle = particles[i];
			particle.age += deltaTime;
			if (particle.lifetime <= particle.age) {

				particle = particles.back();
				particles.pop_back();
				continue;
			}
			particle.position += particle.velocity * deltaTime;
			++i;
		}

		// 生存粒子をモジュールで一括更新する
		for (const auto& module : effect->modules) {
			module->OnUpdate(std::span<Particle>(particles), deltaTime);
		}

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
			if (0 < spawnCount) {

				particles.resize(particles.size() + spawnCount);
				std::span<Particle> newborn(particles.data() + particles.size() - spawnCount, spawnCount);
				// エミッター形状から初期状態を決めてから、各モジュールの発生処理を通す
				InitEmitterParticles(newborn, emitterSettings, asset.space == PrimitiveRenderSpace::Screen2D);
				for (const auto& module : effect->modules) {
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
				for (const auto& module : effect->modules) {
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
			BuildModules(found->second);
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

	// アセットを読み込みモジュールを構築する、未登録のモジュールは読み飛ばす
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
	BuildModules(runtime);
	return runtime;
}

void Engine::ParticleSystem::InitEmitterParticles(std::span<Particle> newborn,
	const ParticleEmitterSettings& settings, bool is2D) const {

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	for (Particle& particle : newborn) {

		Vector3 position = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);

		switch (settings.shape) {
		case ParticleEmitterShape::Sphere: {

			// 球面上から外向きに飛ばす
			direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
			position = direction * settings.sphereRadius;
			break;
		}
		case ParticleEmitterShape::Hemisphere: {

			// Y上向きの半球面から外向きに飛ばす
			direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
			direction.y = std::abs(direction.y);
			position = direction * settings.sphereRadius;
			break;
		}
		case ParticleEmitterShape::Box: {

			// 有効な面からランダムに選び、面上の点から面法線方向へ飛ばす
			const bool faces[6] = {
				settings.boxFacePosX, settings.boxFaceNegX, settings.boxFacePosY,
				settings.boxFaceNegY, settings.boxFacePosZ, settings.boxFaceNegZ };
			int32_t enabledCount = 0;
			for (bool face : faces) { enabledCount += face ? 1 : 0; }
			if (enabledCount == 0) {
				break;
			}
			int32_t pick = RandomGenerator::Generate(0, enabledCount - 1);
			int32_t faceIndex = 0;
			for (int32_t i = 0; i < 6; ++i) {
				if (faces[i] && pick-- == 0) { faceIndex = i; break; }
			}
			const Vector3 half = settings.boxSize * 0.5f;
			position = RandomGenerator::Generate(-half, half);
			switch (faceIndex) {
			case 0: position.x = half.x; direction = Vector3(1.0f, 0.0f, 0.0f); break;
			case 1: position.x = -half.x; direction = Vector3(-1.0f, 0.0f, 0.0f); break;
			case 2: position.y = half.y; direction = Vector3(0.0f, 1.0f, 0.0f); break;
			case 3: position.y = -half.y; direction = Vector3(0.0f, -1.0f, 0.0f); break;
			case 4: position.z = half.z; direction = Vector3(0.0f, 0.0f, 1.0f); break;
			case 5: position.z = -half.z; direction = Vector3(0.0f, 0.0f, -1.0f); break;
			}
			break;
		}
		case ParticleEmitterShape::Torus: {

			// 主円周上の管内から管の外向きに飛ばす
			const float mainAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
			const float tubeAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
			const float tubeRadius = RandomGenerator::Generate(0.0f, settings.torusThickness);
			const Vector3 radial(std::cos(mainAngle), 0.0f, std::sin(mainAngle));
			const Vector3 tubeDir = radial * std::cos(tubeAngle) + Vector3(0.0f, std::sin(tubeAngle), 0.0f);
			position = radial * settings.torusRadius + tubeDir * tubeRadius;
			direction = tubeDir;
			break;
		}
		case ParticleEmitterShape::Circle: {

			// 円弧上から外向きに飛ばす、3DはXZ平面で2DはXY平面
			const float angle = RandomGenerator::Generate(0.0f, settings.circleArc * degToRad);
			direction = is2D ?
				Vector3(std::cos(angle), std::sin(angle), 0.0f) :
				Vector3(std::cos(angle), 0.0f, std::sin(angle));
			position = direction * settings.circleRadius;
			break;
		}
		case ParticleEmitterShape::Rect: {

			// 有効な辺からランダムに選び、辺上の点から辺法線方向へ飛ばす
			const bool edges[4] = {
				settings.rectEdgePosX, settings.rectEdgeNegX, settings.rectEdgePosY, settings.rectEdgeNegY };
			int32_t enabledCount = 0;
			for (bool edge : edges) { enabledCount += edge ? 1 : 0; }
			if (enabledCount == 0) {
				break;
			}
			int32_t pick = RandomGenerator::Generate(0, enabledCount - 1);
			int32_t edgeIndex = 0;
			for (int32_t i = 0; i < 4; ++i) {
				if (edges[i] && pick-- == 0) { edgeIndex = i; break; }
			}
			const Vector2 half = settings.rectSize * 0.5f;
			position = Vector3(RandomGenerator::Generate(-half.x, half.x),
				RandomGenerator::Generate(-half.y, half.y), 0.0f);
			switch (edgeIndex) {
			case 0: position.x = half.x; direction = Vector3(1.0f, 0.0f, 0.0f); break;
			case 1: position.x = -half.x; direction = Vector3(-1.0f, 0.0f, 0.0f); break;
			case 2: position.y = half.y; direction = Vector3(0.0f, 1.0f, 0.0f); break;
			case 3: position.y = -half.y; direction = Vector3(0.0f, -1.0f, 0.0f); break;
			}
			break;
		}
		case ParticleEmitterShape::Cone2D: {

			// 底辺の線分から開き角の範囲で上向きに飛ばす
			position = Vector3(RandomGenerator::Generate(-settings.coneRadius, settings.coneRadius), 0.0f, 0.0f);
			const float tilt = RandomGenerator::Generate(
				-settings.coneAngle * degToRad, settings.coneAngle * degToRad);
			direction = Vector3(std::sin(tilt), std::cos(tilt), 0.0f);
			break;
		}
		case ParticleEmitterShape::Cone: {

			// 底面円から開き角に沿って飛ばす、頂点から離れる方向にする
			const float angle = RandomGenerator::Generate(0.0f, pi * 2.0f);
			const float radius = RandomGenerator::Generate(0.0f, settings.coneRadius);
			const Vector3 radial(std::cos(angle), 0.0f, std::sin(angle));
			position = radial * radius;
			const float coneAngleRad = settings.coneAngle * degToRad;
			if (0.001f < coneAngleRad && 0.001f < settings.coneRadius) {

				const float apexDistance = settings.coneRadius / std::tan(coneAngleRad);
				direction = Vector3::Normalize(position - Vector3(0.0f, -apexDistance, 0.0f));
			} else {
				direction = Vector3(0.0f, 1.0f, 0.0f);
			}
			break;
		}
		case ParticleEmitterShape::Point: {

			// 原点から指定方向へ飛ばす
			direction = Vector3::NormalizeOr(settings.pointDirection, Vector3(0.0f, 1.0f, 0.0f));
			break;
		}
		}

		particle.position = position;
		particle.velocity = direction * settings.speed.Sample();
		particle.lifetime = (std::max)(settings.lifetime.Sample(), 0.001f);
	}
}

void Engine::ParticleSystem::DrawEmitterShape(ECSWorld& world, const Entity& entity,
	const ParticleEmitterSettings& settings, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}

	// エミッターのワールド位置と回転を取り出す
	Vector3 center = Vector3::AnyInit(0.0f);
	Quaternion rotation = Quaternion::Identity();
	if (const auto* transform = world.TryGetComponent<TransformComponent>(entity)) {

		Vector3 scale{};
		DecomposeAffine3D(transform->worldMatrix, center, rotation, scale);
	}
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Color4 color = Color4::Red();
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};

		switch (settings.shape) {
		case ParticleEmitterShape::Circle: {

			constexpr uint32_t kDivision = 24;
			const float arc = settings.circleArc * degToRad;
			const float step = arc / static_cast<float>(kDivision);
			for (uint32_t i = 0; i < kDivision; ++i) {

				const float angle0 = step * static_cast<float>(i);
				const float angle1 = step * static_cast<float>(i + 1);
				renderer2D->DrawLine(
					toScreen(Vector3(std::cos(angle0), std::sin(angle0), 0.0f) * settings.circleRadius),
					toScreen(Vector3(std::cos(angle1), std::sin(angle1), 0.0f) * settings.circleRadius), color);
			}
			break;
		}
		case ParticleEmitterShape::Rect: {

			const Vector2 half = settings.rectSize * 0.5f;
			const Vector3 corners[4] = {
				Vector3(-half.x, -half.y, 0.0f), Vector3(half.x, -half.y, 0.0f),
				Vector3(half.x, half.y, 0.0f), Vector3(-half.x, half.y, 0.0f) };
			for (int32_t i = 0; i < 4; ++i) {
				renderer2D->DrawLine(toScreen(corners[i]), toScreen(corners[(i + 1) % 4]), color);
			}
			break;
		}
		case ParticleEmitterShape::Point: {

			// 射出方向を線で表す、スクリーン単位なので見やすい長さにする
			const Vector3 direction = Vector3::NormalizeOr(settings.pointDirection, Vector3(0.0f, 1.0f, 0.0f));
			renderer2D->DrawCircle(Vector2(center.x, center.y), 4.0f, color);
			renderer2D->DrawLine(toScreen(Vector3::AnyInit(0.0f)), toScreen(direction * 32.0f), color);
			break;
		}
		case ParticleEmitterShape::Cone2D: {

			// 底辺と開き角の2本の線で扇を表す
			const float tilt = settings.coneAngle * degToRad;
			const float rayLength = (std::max)(settings.coneRadius, 32.0f);
			const Vector3 base0(-settings.coneRadius, 0.0f, 0.0f);
			const Vector3 base1(settings.coneRadius, 0.0f, 0.0f);
			renderer2D->DrawLine(toScreen(base0), toScreen(base1), color);
			renderer2D->DrawLine(toScreen(base0),
				toScreen(base0 + Vector3(std::sin(-tilt), std::cos(-tilt), 0.0f) * rayLength), color);
			renderer2D->DrawLine(toScreen(base1),
				toScreen(base1 + Vector3(std::sin(tilt), std::cos(tilt), 0.0f) * rayLength), color);
			break;
		}
		default:
			break;
		}
		return;
	}

	switch (settings.shape) {
	case ParticleEmitterShape::Sphere:
		renderer->DrawSphere(center, settings.sphereRadius, color, 8u);
		break;
	case ParticleEmitterShape::Hemisphere:
		renderer->DrawHemisphere(center, settings.sphereRadius, rotation, color);
		break;
	case ParticleEmitterShape::Box:
		renderer->DrawOBB(center, settings.boxSize * 0.5f, rotation, color);
		break;
	case ParticleEmitterShape::Torus: {

		// 主円周を管の内外2本の円で表す
		constexpr uint32_t kDivision = 24;
		constexpr float kStep = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kDivision);
		for (uint32_t i = 0; i < kDivision; ++i) {

			const float angle0 = kStep * static_cast<float>(i);
			const float angle1 = kStep * static_cast<float>(i + 1);
			for (const float radius : { settings.torusRadius - settings.torusThickness,
				settings.torusRadius + settings.torusThickness }) {

				const Vector3 p0 = center + Vector3::Transform(
					Vector3(std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius), rotationMatrix);
				const Vector3 p1 = center + Vector3::Transform(
					Vector3(std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius), rotationMatrix);
				renderer->DrawLine(p0, p1, color);
			}
		}
		break;
	}
	case ParticleEmitterShape::Circle: {

		// 円弧の範囲だけ線を張る
		constexpr uint32_t kDivision = 24;
		const float arc = settings.circleArc * degToRad;
		const float step = arc / static_cast<float>(kDivision);
		for (uint32_t i = 0; i < kDivision; ++i) {

			const float angle0 = step * static_cast<float>(i);
			const float angle1 = step * static_cast<float>(i + 1);
			const Vector3 local0 = is2D ?
				Vector3(std::cos(angle0), std::sin(angle0), 0.0f) : Vector3(std::cos(angle0), 0.0f, std::sin(angle0));
			const Vector3 local1 = is2D ?
				Vector3(std::cos(angle1), std::sin(angle1), 0.0f) : Vector3(std::cos(angle1), 0.0f, std::sin(angle1));
			renderer->DrawLine(center + Vector3::Transform(local0 * settings.circleRadius, rotationMatrix),
				center + Vector3::Transform(local1 * settings.circleRadius, rotationMatrix), color);
		}
		break;
	}
	case ParticleEmitterShape::Rect: {

		// 矩形の外周を線で表す
		const Vector2 half = settings.rectSize * 0.5f;
		const Vector3 corners[4] = {
			Vector3(-half.x, -half.y, 0.0f), Vector3(half.x, -half.y, 0.0f),
			Vector3(half.x, half.y, 0.0f), Vector3(-half.x, half.y, 0.0f) };
		for (int32_t i = 0; i < 4; ++i) {
			renderer->DrawLine(center + Vector3::Transform(corners[i], rotationMatrix),
				center + Vector3::Transform(corners[(i + 1) % 4], rotationMatrix), color);
		}
		break;
	}
	case ParticleEmitterShape::Cone2D: {

		// 底辺と開き角の2本の線で扇を表す
		const float tilt = settings.coneAngle * degToRad;
		const Vector3 base0(-settings.coneRadius, 0.0f, 0.0f);
		const Vector3 base1(settings.coneRadius, 0.0f, 0.0f);
		renderer->DrawLine(center + Vector3::Transform(base0, rotationMatrix),
			center + Vector3::Transform(base1, rotationMatrix), color);
		renderer->DrawLine(center + Vector3::Transform(base0, rotationMatrix),
			center + Vector3::Transform(base0 + Vector3(std::sin(-tilt), std::cos(-tilt), 0.0f), rotationMatrix), color);
		renderer->DrawLine(center + Vector3::Transform(base1, rotationMatrix),
			center + Vector3::Transform(base1 + Vector3(std::sin(tilt), std::cos(tilt), 0.0f), rotationMatrix), color);
		break;
	}
	case ParticleEmitterShape::Cone: {

		// 開き角に沿った上面半径で高さ1の円錐を表す
		const float displayHeight = 1.0f;
		const float topRadius = settings.coneRadius + std::tan(settings.coneAngle * degToRad) * displayHeight;
		renderer->DrawCone(center, settings.coneRadius, topRadius, displayHeight, rotation, color);
		break;
	}
	case ParticleEmitterShape::Point: {

		// 射出方向を線で表す
		const Vector3 direction = Vector3::NormalizeOr(settings.pointDirection, Vector3(0.0f, 1.0f, 0.0f));
		renderer->DrawSphere(center, 0.05f, color, 8u);
		renderer->DrawLine(center, center + Vector3::Transform(direction, rotationMatrix), color);
		break;
	}
	}
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

void Engine::ParticleSystem::BuildModules(EffectRuntime& runtime) const {

	// 未登録のモジュールは読み飛ばす
	runtime.modules.clear();
	for (const ParticleEffectModuleEntry& entry : runtime.asset.modules) {

		auto module = ParticleModuleRegistry::GetInstance().Create(entry.id);
		if (!module) {
			continue;
		}
		module->FromJson(entry.params);
		runtime.modules.emplace_back(std::move(module));
	}
}
