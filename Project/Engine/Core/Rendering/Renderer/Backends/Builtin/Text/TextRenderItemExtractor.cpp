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
		// 個別マテリアルパラメータはコンポーネントのmapを指す、描画時に既定値へ重ねる
		payload.materialOverrides = &renderer.parameterOverrides;
		// 描画アイテムの構築
		RenderItem item{};
		const UIElementRuntime* uiRuntime = UIRuntimeService::GetInstance().Find(world, entity);
		const Matrix4x4 worldMatrix = uiRuntime ? uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(world, entity);
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, worldMatrix);
		item.backendID = RenderBackendID::Text;
		item.material = renderer.material;
		if (uiRuntime) {

			item.renderPhase = RenderPhase::ScreenUI;
			item.cameraDomain = RenderCameraDomain::Screen;
			item.sortingLayer += uiRuntime->canvasSortingLayer;
			item.sortingOrder += uiRuntime->canvasOrder;
			item.orderedUI = true;
			item.hierarchyOrder = uiRuntime->hierarchyOrder;
		// 2DはOrthographicでScreenUI、3DはPerspectiveで深度ありのTransparentパスに乗せる
		} else if (renderer.dimension == Dimension::Type3D) {

			item.cameraDomain = RenderCameraDomain::Perspective;
			item.renderPhase = RenderPhase::Transparent;
		} else {

			item.cameraDomain = RenderCameraDomain::Orthographic;
		}
		// 個別マテリアルパラメータを持つアイテムは専用cbufferが要るので、エンティティ単位で一意化して単独描画にする
		item.batchKey = renderer.parameterOverrides.empty() ? std::hash<AssetID>{}(renderer.font) :
			((static_cast<uint64_t>(entity.generation) << 32) | entity.index);
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}
