#include "SceneSkyboxResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SceneSkyboxResolver classMethods
//============================================================================
Engine::SceneSkyboxInfo Engine::SceneSkyboxResolver::Resolve(
	GraphicsCore& graphicsCore, AssetDatabase* assetDatabase, ECSWorld* world, uint32_t cullingMask) {

	SceneSkyboxInfo info{};
	if (!world || !assetDatabase) {
		return info;
	}

	// 背景と環境光で同じSkyboxを選ぶ
	const SkyboxRendererComponent* skybox = Find(*world, cullingMask);
	if (!skybox) {
		return info;
	}

	// cubemapテクスチャを解決する
	const GPUTextureResource* cubemap = RuntimeTextureResolver::Resolve(
		graphicsCore, assetDatabase, skybox->cubemapTexture,
		TextureColorSpace::Linear);
	if (!cubemap || cubemap->srvIndex == UINT32_MAX) {
		return info;
	}

	info.cubemapIndex = cubemap->srvIndex;
	info.cubemapAssetID = skybox->cubemapTexture;
	info.color = skybox->color;
	info.iblIntensity = skybox->iblIntensity;
	info.found = true;
	return info;
}

const Engine::SkyboxRendererComponent* Engine::SceneSkyboxResolver::Find(ECSWorld& world, uint32_t cullingMask) {

	const SkyboxRendererComponent* skybox = nullptr;
	world.ForEach<SkyboxRendererComponent>([&](const Entity& entity, const SkyboxRendererComponent& component) {

		// 他のRendererと同じ可視レイヤーで選別する
		if (skybox || !component.cubemapTexture || !RenderItemExtract::IsVisible(world, entity, component.visible) ||
			(RenderItemExtract::GetVisibilityLayerMask(world, entity, component.renderingLayerMask) & cullingMask) == 0) {
			return;
		}
		skybox = &component;
	});
	return skybox;
}
