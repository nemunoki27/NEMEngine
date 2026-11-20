#include "DecalSettingsMigration.h"

//============================================================================
//	include
//============================================================================
#include "DecalProjection.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <cmath>

namespace {

	using namespace Engine;

	template <typename T>
	void ApplyMultiplier(MaterialParameterSet& instance, const MaterialParameterSet& defaults, const char* name, T multiplier, T identity) {

		if (multiplier == identity) return;
		const auto id = MaterialParameterID::FromName(name);
		const auto* parameter = instance.Find(id);
		if (!parameter) parameter = defaults.Find(id);
		const auto* value = parameter ? std::get_if<T>(&parameter->value) : nullptr;
		instance.Set(name, MaterialParameterValue{.value = (value ? *value : identity) * multiplier});
	}
}

void Engine::MigrateDecalSettings(ECSWorld& world, Entity entity, AssetDatabase* database) {

	auto* decal = world.TryGetComponent<DecalProjectorComponent>(entity);
	if (!decal) return;
	if (database && (decal->color != Color4::White() || decal->fadeFactor != 1.0f)) {
		RenderAssetLibrary assets;
		assets.Init(database);
		const auto id = MaterialResolver{}.ResolveORDefault(*database, decal->material, DefaultMaterialSlot::Decal);
		if (const auto* material = assets.LoadMaterial(id)) {
			ApplyMultiplier(decal->materialInstance, material->parameters, "color", decal->color, Color4::White());
			ApplyMultiplier(decal->materialInstance, material->parameters, "opacity", decal->fadeFactor, 1.0f);
			decal->color = Color4::White();
			decal->fadeFactor = 1.0f;
		}
	}
	if (!decal->hasLegacyUV) return;
	const Matrix4x4 legacy = DecalProjectionUtility::LegacyUVMatrix(*decal);
	UVTransformComponent shared{};
	if (const auto* uv = world.TryGetComponent<UVTransformComponent>(entity)) {
		shared = *uv;
	} else if (legacy != Matrix4x4::Identity()) {
		const bool sharesUV = world.HasComponent(entity, "MeshRenderer") || world.HasComponent(entity, "PrimitiveRenderer") ||
			world.HasComponent(entity, "SpriteRenderer") || world.HasComponent(entity, "TextRenderer") ||
			world.HasComponent(entity, "BeamRenderer") || world.HasComponent(entity, "TrailRenderer") ||
			world.HasComponent(entity, "ParticleSystem") || world.HasComponent(entity, "FlipbookAnimation");
		if (!sharesUV) {
			shared.pos = {legacy.m[3][0], legacy.m[3][1]};
			shared.rotation = -decal->uvRotation;
			shared.scale = decal->uvScale;
		}
		shared.uvMatrix = MakeUVMatrix(shared);
		world.AddComponent<UVTransformComponent>(entity) = shared;
		// コンポーネント追加でアーキタイプが移動する
		decal = world.TryGetComponent<DecalProjectorComponent>(entity);
	}
	// 旧UVとの変換差を計算する。ゼロスケールの軸は移行・描画とも単位軸を加える
	decal->uvCorrectionScaleOffset = {std::abs(shared.scale.x) < 1e-6f ? 1.0f : 0.0f,
		std::abs(shared.scale.y) < 1e-6f ? 1.0f : 0.0f};
	shared.scale += decal->uvCorrectionScaleOffset;
	Matrix4x4 inverse;
	if (!Matrix4x4::TryInverse(MakeUVMatrix(shared), inverse)) return;
	const auto difference = legacy * inverse - Matrix4x4::Identity();
	decal->uvCorrectionU = {difference.m[0][0], difference.m[1][0], difference.m[3][0]};
	decal->uvCorrectionV = {difference.m[0][1], difference.m[1][1], difference.m[3][1]};
	decal->hasLegacyUV = false;
	decal->uvBias = {};
	decal->uvRotation = 0;
	decal->uvScale = {1, 1};
}
