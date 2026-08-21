#include "SceneSkyboxResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SceneSkyboxResolver classMethods
//============================================================================
Engine::SceneSkyboxInfo Engine::SceneSkyboxResolver::Resolve(
	GraphicsCore& graphicsCore, AssetDatabase* assetDatabase, ECSWorld* world) {

	SceneSkyboxInfo info{};
	if (!world || !assetDatabase) {
		return info;
	}

	// 最初に見つかった有効なskyboxを対象にする
	SkyboxRendererComponent* skybox = nullptr;
	world->ForEach<SkyboxRendererComponent>([&](Entity entity, SkyboxRendererComponent& component) {

		if (skybox || !component.visible || !component.cubemapTexture) {
			return;
		}
		if (!IsEntityActiveInHierarchy(*world, entity)) {
			return;
		}
		skybox = &component;
		});
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
