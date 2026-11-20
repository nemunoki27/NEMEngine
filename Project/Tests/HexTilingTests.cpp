#include "HexTilingTests.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Components/Rendering/HexTilingComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>

// c++
#include <array>
#include <bit>
#include <numbers>

bool NEMTests::TestHexTiling() {

	using namespace Engine;
	ECSWorld world;
	const auto entity = world.CreateEntity();
	if (ResolveHexTiling(world, entity).x != 0.0f) return false;
	world.AddComponent<MeshRendererComponent>(entity);
	world.AddComponent<HexTilingComponent>(entity);
	std::array<SubMeshMaterial, 3> subMeshes{};
	subMeshes[0].stableID = {93};
	subMeshes[1].stableID = {41};
	SetMeshSubMeshes(world, entity, subMeshes);
	auto& component = world.GetComponent<HexTilingComponent>(entity);
	const auto defaults = ResolveHexTiling(world, entity, 0);
	if (defaults.x != 1.0f || defaults.y != 0.0f || defaults.z != 0.0f) return false;
	component.tileSize = 3.5f;
	component.randomRotation = 180.0f;
	component.seed = -1;
	SetHexTilingSubMeshEnabled(component, subMeshes[0], 0, false);
	SetHexTilingSubMeshEnabled(component, subMeshes[2], 2, false);
	component = nlohmann::json(component).get<HexTilingComponent>();
	if (ResolveHexTiling(world, entity, 0).x != 0.0f || ResolveHexTiling(world, entity, 2).x != 0.0f) return false;
	std::swap(subMeshes[0], subMeshes[1]);
	SetMeshSubMeshes(world, entity, subMeshes);
	const auto packed = ResolveHexTiling(world, entity, 0);
	if (packed.x != 3.5f || packed.y != std::numbers::pi_v<float> || std::bit_cast<int32_t>(packed.z) != -1 ||
		ResolveHexTiling(world, entity, 1).x != 0.0f) return false;
	SetHexTilingSubMeshEnabled(component, subMeshes[1], 1, true);
	if (ResolveHexTiling(world, entity, 1).x != 3.5f) return false;
	component.enabled = false;
	return ResolveHexTiling(world, entity).x == 0.0f;
}

bool NEMTests::TestHexTilingManaged() {

	return RunManagedComponentProbe("HexTiling", "NEMTests.Scripts.HexTilingAPIProbe",
		"5193fde1-8360-4618-a568-fbaa273448b0", "6a55be91-44d3-4b5a-a14d-365d0aea59f6",
		[](Engine::ECSWorld& world) {
			const auto target = Engine::SceneAuthoring::CreateGameObject(world, "HexMesh");
			world.AddComponent<Engine::MeshRendererComponent>(target);
			std::array<Engine::SubMeshMaterial, 2> subMeshes{};
			subMeshes[1].stableID = {41};
			Engine::SetMeshSubMeshes(world, target, subMeshes);
		});
}
