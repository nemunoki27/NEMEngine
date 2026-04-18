#include "TextRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>

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
		payload.color = renderer.color;
		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, RenderItemExtract::GetWorldMatrix(world, entity));
		item.backendID = RenderBackendID::Text;
		item.material = renderer.material;
		item.cameraDomain = RenderCameraDomain::Orthographic;
		item.batchKey = renderer.font.value;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});
}