#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <algorithm>

namespace NEMTests {

	bool TestRendererLayerCulling() {

		using namespace Engine;
		ECSWorld world(ECSWorldKind::Runtime);
		const Entity entity = world.CreateEntity();
		world.AddComponent<SceneObjectComponent>(entity);
		world.AddComponent<MeshRendererComponent>(entity);
		world.AddComponent<PrimitiveRendererComponent>(entity);
		world.AddComponent<SpriteRendererComponent>(entity);
		world.AddComponent<TextRendererComponent>(entity);
		world.AddComponent<LineRendererComponent>(entity);
		world.AddComponent<ParticleSystemComponent>(entity);
		world.GetComponent<MeshRendererComponent>(entity).renderingLayerMask = 8;
		world.GetComponent<PrimitiveRendererComponent>(entity).renderingLayerMask = 8;
		world.GetComponent<SpriteRendererComponent>(entity).renderingLayerMask = 8;
		world.GetComponent<TextRendererComponent>(entity).renderingLayerMask = 8;
		world.GetComponent<LineRendererComponent>(entity).renderingLayerMask = 8;
		world.GetComponent<ParticleSystemComponent>(entity).renderingLayerMask = 8;
		const std::vector<LinePoint> points = { { Vector3(0, 0, 0), Color4::White() },
			{ Vector3(1, 0, 0), Color4::White() } };
		SetLinePoints(world, entity, points);
		ParticleGroupRuntimeState group{};
		group.groupID = Engine::UUID{ 1 };
		TryGetParticleSystemRuntime(world, entity)->effect.runtimeGroups.push_back(group);

		ResolvedRenderView left{};
		left.valid = true;
		left.perspective.valid = true;
		left.perspective.cullingMask = 1 | 4;
		left.orthographic.valid = true;
		left.screen.valid = true;
		ResolvedRenderView right = left;
		right.perspective.cullingMask = 1 | 8;
		RenderSceneBatch batch;
		MeshRenderItemExtractor{}.Extract(world, batch);
		PrimitiveRenderItemExtractor{}.Extract(world, batch);
		SpriteRenderItemExtractor{}.Extract(world, batch);
		TextRenderItemExtractor{}.Extract(world, batch);
		LineRenderItemExtractor{}.Extract(world, batch);
		ParticleRenderItemExtractor{}.Extract(world, batch);

		// 全Rendererの抽出結果を左右のCameraで選別する
		if (batch.GetItems().size() != 6) return false;
		RenderPassItemList leftItems;
		RenderPassItemList rightItems;
		for (const RenderItem& item : batch.GetItems()) {

			RenderPassItemCollector::CollectForView(batch, item.renderPhase, left, leftItems);
			RenderPassItemCollector::CollectForView(batch, item.renderPhase, right, rightItems);
			if (item.visibilityLayerMask != 8 || !leftItems.IsEmpty() ||
				std::find(rightItems.items.begin(), rightItems.items.end(), &item) == rightItems.items.end()) return false;
		}
		// 画面UIも出力先Cameraのカリングマスクを使う
		RenderItem screen{};
		screen.cameraDomain = RenderCameraDomain::Screen;
		screen.visibilityLayerMask = 8;
		RenderSceneBatch screenBatch;
		screenBatch.Add(RenderItem(screen));
		RenderPassItemCollector::CollectForView(screenBatch, screen.renderPhase, left, leftItems);
		RenderPassItemCollector::CollectForView(screenBatch, screen.renderPhase, right, rightItems);
		if (!leftItems.IsEmpty() || rightItems.items.size() != 1) return false;
		right.kind = RenderViewKind::Scene;
		RenderPassItemCollector::CollectForView(screenBatch, screen.renderPhase, right, rightItems);
		if (rightItems.items.size() != 1) return false;

		// Scene側で非表示のレイヤーはRendererから復活させない
		world.GetComponent<SceneObjectComponent>(entity).visibilityLayerMask = 4;
		batch.Clear();
		PrimitiveRenderItemExtractor{}.Extract(world, batch);
		if (batch.GetItems().size() != 1) return false;
		RenderPassItemCollector::CollectForView(batch, batch.GetItems().front().renderPhase, right, rightItems);
		if (!rightItems.IsEmpty()) return false;

		// 背景と反射で同じレイヤーのSkyboxを選ぶ
		const Entity leftSky = world.CreateEntity();
		world.AddComponent<SkyboxRendererComponent>(leftSky);
		world.GetComponent<SkyboxRendererComponent>(leftSky).cubemapTexture = AssetID{ 1, 2 };
		world.GetComponent<SkyboxRendererComponent>(leftSky).renderingLayerMask = 4;
		const Entity rightSky = world.CreateEntity();
		world.AddComponent<SkyboxRendererComponent>(rightSky);
		world.GetComponent<SkyboxRendererComponent>(rightSky).cubemapTexture = AssetID{ 3, 4 };
		world.GetComponent<SkyboxRendererComponent>(rightSky).renderingLayerMask = 8;
		const auto* selectedLeft = SceneSkyboxResolver::Find(world, 1 | 4);
		const auto* selectedRight = SceneSkyboxResolver::Find(world, 1 | 8);
		if (!selectedLeft || !selectedRight || selectedLeft->cubemapTexture != AssetID{ 1, 2 } ||
			selectedRight->cubemapTexture != AssetID{ 3, 4 } || SceneSkyboxResolver::Find(world, 1)) return false;
		const nlohmann::json particleData = world.GetComponent<ParticleSystemComponent>(entity);
		const nlohmann::json skyData = world.GetComponent<SkyboxRendererComponent>(rightSky);
		return particleData.get<ParticleSystemComponent>().renderingLayerMask == 8 &&
			skyData.get<SkyboxRendererComponent>().renderingLayerMask == 8;
	}
} // NEMTests
