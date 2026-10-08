#include "TextRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>

//============================================================================
//	TextRenderItemExtractor classMethods
//============================================================================
void Engine::TextRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<TextRendererComponent>([&](const Entity& entity, const TextRendererComponent& renderer) {

		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}

		// ペイロード構築
		TextRenderPayload payload{};
		payload.font = renderer.font;
		payload.text = std::string_view(renderer.text);
		payload.fontSize = renderer.fontSize;
		payload.charSpacing = renderer.charSpacing;
		if (const UVTransformComponent* uvTransform = world.TryGetComponent<UVTransformComponent>(entity)) {
			payload.uvMatrix = uvTransform->uvMatrix;
		}
		// Rendererの上書き値を参照する
		payload.materialInstance = &renderer.materialInstance.Get();
		// 描画アイテムの構築
		RenderItem item{};
		const UIElementRuntime* uiRuntime = renderer.dimension == Dimension::Type2D ?
			UIRuntimeService::GetInstance().Find(world, entity) : nullptr;
		const Matrix4x4 worldMatrix = uiRuntime ? uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(world, entity);
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, worldMatrix);
		item.backendID = RenderBackendID::Text;
		item.material = renderer.material;
		if (uiRuntime) {

			item.cameraDomain = RenderCameraDomain::Screen;
			item.sortingLayer += uiRuntime->canvasSortingLayer;
			item.sortingOrder += uiRuntime->canvasOrder;
			item.orderedUI = true;
			item.hierarchyOrder = uiRuntime->hierarchyOrder;
		} else if (renderer.dimension == Dimension::Type3D) {

			// 3Dは深度付きの透過描画へ渡す
			item.cameraDomain = RenderCameraDomain::Perspective;
			if (item.renderPhase == RenderPhase::ScreenUI) {
				item.renderPhase = RenderPhase::Transparent;
			}
		} else {

			item.cameraDomain = RenderCameraDomain::Orthographic;
		}
		// 同じフォントと上書き値で描画をまとめる
		const uint64_t fontHash = std::hash<AssetID>{}(renderer.font);
		item.batchKey = Algorithm::MixHash(fontHash, renderer.materialInstance.GetContentHash());
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
