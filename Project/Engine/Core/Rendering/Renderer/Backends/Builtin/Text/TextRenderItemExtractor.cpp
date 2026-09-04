#include "TextRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>

//============================================================================
//	TextRenderItemExtractor classMethods
//============================================================================
void Engine::TextRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<TextRendererComponent>([&](const Entity& entity, TextRendererComponent& renderer) {

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
		// Renderer固有Material Instanceを描画時に既定値へ重ねる
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
		// 2DはOrthographicでScreenUI、3DはPerspectiveで深度ありのTransparentパスに乗せる
		} else if (renderer.dimension == Dimension::Type3D) {

			item.cameraDomain = RenderCameraDomain::Perspective;
			if (item.renderPhase == RenderPhase::ScreenUI) {
				item.renderPhase = RenderPhase::Transparent;
			}
		} else {

			item.cameraDomain = RenderCameraDomain::Orthographic;
		}
		// フォントとMaterial Instance値が同じTextを同一バッチへまとめる
		const uint64_t fontHash = std::hash<AssetID>{}(renderer.font);
		item.batchKey = fontHash ^
			(renderer.materialInstance.GetContentHash() + 0x9e3779b97f4a7c15ull +
				(fontHash << 6) + (fontHash >> 2));
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
