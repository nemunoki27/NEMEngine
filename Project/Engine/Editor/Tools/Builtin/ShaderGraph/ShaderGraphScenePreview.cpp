#include "ShaderGraphScenePreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

namespace {

	bool ReadRendererMaterial(
		Engine::ECSWorld& world,
		const Engine::Entity& entity,
		Engine::ShaderGraphTarget target,
		Engine::AssetID& outMaterial) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			if (const auto* renderer =
				world.TryGetComponent<
				Engine::MeshRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive3D:
			if (const auto* renderer =
				world.TryGetComponent<
				Engine::PrimitiveRendererComponent>(entity)) {

				if (Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive2D:
			if (const auto* renderer =
				world.TryGetComponent<
				Engine::PrimitiveRendererComponent>(entity)) {

				if (!Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Sprite:
			if (const auto* renderer =
				world.TryGetComponent<
				Engine::SpriteRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Text:
			if (const auto* renderer =
				world.TryGetComponent<
				Engine::TextRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		}
		return false;
	}

	bool WriteRendererMaterial(
		Engine::ECSWorld& world,
		const Engine::Entity& entity,
		Engine::ShaderGraphTarget target,
		Engine::AssetID material) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			if (auto* renderer =
				world.TryGetComponent<
				Engine::MeshRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::MeshRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive3D:
			if (auto* renderer =
				world.TryGetComponent<
				Engine::PrimitiveRendererComponent>(entity)) {

				if (Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				renderer->material = material;
				world.MarkComponentModified<
					Engine::PrimitiveRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive2D:
			if (auto* renderer =
				world.TryGetComponent<
				Engine::PrimitiveRendererComponent>(entity)) {

				if (!Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				renderer->material = material;
				world.MarkComponentModified<
					Engine::PrimitiveRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Sprite:
			if (auto* renderer =
				world.TryGetComponent<
				Engine::SpriteRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::SpriteRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Text:
			if (auto* renderer =
				world.TryGetComponent<
				Engine::TextRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::TextRendererComponent>(entity);
				return true;
			}
			break;
		}
		return false;
	}
}

bool Engine::ShaderGraphScenePreview::ApplyPreviewMaterial(
	const EditorToolContext& context, ShaderGraphTarget target, AssetID material, std::string& statusMessage) {

	ECSWorld* world = context.GetWorld();
	if (!world || !previewEntityUUID_ ||
		!material) {

		return false;
	}
	const Entity entity =
		world->FindByUUID(previewEntityUUID_);
	if (!world->IsAlive(entity)) {
		previewEntityUUID_ = {};
		previewMaterialApplied_ = false;
		return false;
	}

	if (previewMaterialApplied_ &&
		(appliedPreviewEntityUUID_ !=
			previewEntityUUID_ ||
			appliedPreviewTarget_ != target)) {

		RestorePreviewMaterial(context);
	}
	if (!previewMaterialApplied_) {
		if (!ReadRendererMaterial(
			*world, entity, target,
			previewOriginalMaterial_)) {

			statusMessage =
				"描画対象に対応するRendererがありません";
			return false;
		}
		appliedPreviewEntityUUID_ =
			previewEntityUUID_;
		appliedPreviewTarget_ = target;
		previewMaterialApplied_ = true;
	}
	if (!WriteRendererMaterial(
		*world, entity, target,
		material)) {

		previewMaterialApplied_ = false;
		return false;
	}
	statusMessage =
		"マテリアルをプレビューしています";
	return true;
}

void Engine::ShaderGraphScenePreview::RestorePreviewMaterial(const EditorToolContext& context) {

	if (!previewMaterialApplied_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (world) {
		const Entity entity =
			world->FindByUUID(
				appliedPreviewEntityUUID_);
		if (world->IsAlive(entity)) {
			WriteRendererMaterial(
				*world, entity,
				appliedPreviewTarget_,
				previewOriginalMaterial_);
		}
	}
	appliedPreviewEntityUUID_ = {};
	previewOriginalMaterial_ = {};
	previewMaterialApplied_ = false;
	previewCompileDeadline_ = 0.0;
}
