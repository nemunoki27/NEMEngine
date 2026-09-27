#include "ClearRenderingLayerCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

Engine::ClearRenderingLayerCommand::ClearRenderingLayerCommand(
	uint32_t layerIndex) :
	layerIndex_(layerIndex) {
}

bool Engine::ClearRenderingLayerCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene() || layerIndex_ >= 32u) {
		return false;
	}
	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}
	if (!captured_) {
		// 対象Entityと変更前のbit maskを保存
		world->ForEach<MeshRendererComponent>([&](Entity entity, MeshRendererComponent& component) {
			if ((component.renderingLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::Mesh, component.renderingLayerMask });
			}
		});
		world->ForEach<SpriteRendererComponent>([&](Entity entity, SpriteRendererComponent& component) {
			if ((component.renderingLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::Sprite, component.renderingLayerMask });
			}
		});
		world->ForEach<PrimitiveRendererComponent>([&](Entity entity, PrimitiveRendererComponent& component) {
			if ((component.renderingLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::Primitive, component.renderingLayerMask });
			}
		});
		world->ForEach<TextRendererComponent>([&](Entity entity, TextRendererComponent& component) {
			if ((component.renderingLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::Text, component.renderingLayerMask });
			}
		});
		world->ForEach<LineRendererComponent>([&](Entity entity, LineRendererComponent& component) {
			if ((component.renderingLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::Line, component.renderingLayerMask });
			}
		});
		world->ForEach<DirectionalLightComponent>([&](Entity entity, DirectionalLightComponent& component) {
			if ((component.affectLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::DirectionalLight, component.affectLayerMask });
			}
		});
		world->ForEach<PointLightComponent>([&](Entity entity, PointLightComponent& component) {
			if ((component.affectLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::PointLight, component.affectLayerMask });
			}
		});
		world->ForEach<SpotLightComponent>([&](Entity entity, SpotLightComponent& component) {
			if ((component.affectLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::SpotLight, component.affectLayerMask });
			}
		});
		world->ForEach<RectLightComponent>([&](Entity entity, RectLightComponent& component) {
			if ((component.affectLayerMask & (1u << layerIndex_)) != 0u) {
				entries_.push_back({ world->GetUUID(entity), TargetKind::RectLight, component.affectLayerMask });
			}
		});
		captured_ = true;
		if (entries_.empty()) {
			return false;
		}
	}
	return Apply(context, true);
}

void Engine::ClearRenderingLayerCommand::Undo(EditorCommandContext& context) {

	Apply(context, false);
}

bool Engine::ClearRenderingLayerCommand::Redo(EditorCommandContext& context) {

	return Apply(context, true);
}

bool Engine::ClearRenderingLayerCommand::Apply(
	EditorCommandContext& context, bool clearBit) {

	if (!context.CanEditScene()) {
		return false;
	}
	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}
	const uint32_t bit = 1u << layerIndex_;
	for (const Entry& entry : entries_) {
		const Entity entity = world->FindByUUID(entry.entity);
		if (!world->IsAlive(entity)) {
			continue;
		}
		// Undoは保存したmaskへ戻し、Redoは対象bitだけを落とす
		const uint32_t mask = clearBit ? entry.previousMask & ~bit : entry.previousMask;
		switch (entry.kind) {
		case TargetKind::Mesh:
			if (auto* value = world->TryGetComponent<MeshRendererComponent>(entity)) {
				value->renderingLayerMask = mask;
				world->MarkComponentModified<MeshRendererComponent>(entity);
			}
			break;
		case TargetKind::Sprite:
			if (auto* value = world->TryGetComponent<SpriteRendererComponent>(entity)) {
				value->renderingLayerMask = mask;
				world->MarkComponentModified<SpriteRendererComponent>(entity);
			}
			break;
		case TargetKind::Primitive:
			if (auto* value = world->TryGetComponent<PrimitiveRendererComponent>(entity)) {
				value->renderingLayerMask = mask;
				world->MarkComponentModified<PrimitiveRendererComponent>(entity);
			}
			break;
		case TargetKind::Text:
			if (auto* value = world->TryGetComponent<TextRendererComponent>(entity)) {
				value->renderingLayerMask = mask;
				world->MarkComponentModified<TextRendererComponent>(entity);
			}
			break;
		case TargetKind::Line:
			if (auto* value = world->TryGetComponent<LineRendererComponent>(entity)) {
				value->renderingLayerMask = mask;
				world->MarkComponentModified<LineRendererComponent>(entity);
			}
			break;
		case TargetKind::DirectionalLight:
			if (auto* value = world->TryGetComponent<DirectionalLightComponent>(entity)) {
				value->affectLayerMask = mask;
				world->MarkComponentModified<DirectionalLightComponent>(entity);
			}
			break;
		case TargetKind::PointLight:
			if (auto* value = world->TryGetComponent<PointLightComponent>(entity)) {
				value->affectLayerMask = mask;
				world->MarkComponentModified<PointLightComponent>(entity);
			}
			break;
		case TargetKind::SpotLight:
			if (auto* value = world->TryGetComponent<SpotLightComponent>(entity)) {
				value->affectLayerMask = mask;
				world->MarkComponentModified<SpotLightComponent>(entity);
			}
			break;
		case TargetKind::RectLight:
			if (auto* value = world->TryGetComponent<RectLightComponent>(entity)) {
				value->affectLayerMask = mask;
				world->MarkComponentModified<RectLightComponent>(entity);
			}
			break;
		}
	}
	return true;
}
