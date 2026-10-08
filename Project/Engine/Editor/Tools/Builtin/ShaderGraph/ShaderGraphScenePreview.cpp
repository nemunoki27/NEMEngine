#include "ShaderGraphScenePreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// c++
#include <type_traits>

namespace {

	// Primitiveの描画領域とプレビュー対象を照合する
	template<typename T>
	bool MatchesTarget([[maybe_unused]] const T& renderer, [[maybe_unused]] Engine::ShaderGraphTarget target) {

		if constexpr (std::is_same_v<T, Engine::PrimitiveRendererComponent>) {
			return Engine::IsPrimitiveScreen2D(renderer) == (target == Engine::ShaderGraphTarget::Primitive2D);
		} else {
			return true;
		}
	}

	// RendererのMaterialを読取専用で取得する
	template<typename T>
	bool ReadRendererMaterial(const Engine::ECSWorld& world, Engine::Entity entity,
		Engine::ShaderGraphTarget target, Engine::AssetID& material) {

		const T* renderer = world.TryGetComponent<T>(entity);
		if (!renderer || !MatchesTarget(*renderer, target)) { return false; }
		material = renderer->material;
		return true;
	}

	// Rendererを変更して差分通知を送る
	template<typename T>
	bool WriteRendererMaterial(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::ShaderGraphTarget target, Engine::AssetID material, bool requireTarget) {

		T* renderer = world.TryGetComponent<T>(entity);
		if (!renderer || (requireTarget && !MatchesTarget(*renderer, target))) { return false; }
		renderer->material = material;
		world.MarkComponentModified<T>(entity);
		return true;
	}

	// 対象の種類から読取先を選ぶ
	bool ReadRendererMaterial(const Engine::ECSWorld& world, Engine::Entity entity,
		Engine::ShaderGraphTarget target, Engine::AssetID& material) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			return ReadRendererMaterial<Engine::MeshRendererComponent>(world, entity, target, material);
		case Engine::ShaderGraphTarget::Primitive3D:
		case Engine::ShaderGraphTarget::Primitive2D:
			return ReadRendererMaterial<Engine::PrimitiveRendererComponent>(world, entity, target, material);
		case Engine::ShaderGraphTarget::Sprite:
			return ReadRendererMaterial<Engine::SpriteRendererComponent>(world, entity, target, material);
		case Engine::ShaderGraphTarget::Text:
			return ReadRendererMaterial<Engine::TextRendererComponent>(world, entity, target, material);
		}
		return false;
	}

	// 対象の種類から書込先を選ぶ
	bool WriteRendererMaterial(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::ShaderGraphTarget target, Engine::AssetID material, bool requireTarget = true) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			return WriteRendererMaterial<Engine::MeshRendererComponent>(world, entity, target, material, requireTarget);
		case Engine::ShaderGraphTarget::Primitive3D:
		case Engine::ShaderGraphTarget::Primitive2D:
			return WriteRendererMaterial<Engine::PrimitiveRendererComponent>(world, entity, target, material, requireTarget);
		case Engine::ShaderGraphTarget::Sprite:
			return WriteRendererMaterial<Engine::SpriteRendererComponent>(world, entity, target, material, requireTarget);
		case Engine::ShaderGraphTarget::Text:
			return WriteRendererMaterial<Engine::TextRendererComponent>(world, entity, target, material, requireTarget);
		}
		return false;
	}
}

//============================================================================
//	ShaderGraphScenePreview classMethods
//============================================================================
Engine::ShaderGraphScenePreview::~ShaderGraphScenePreview() {

	// 生存中のWorldへ元のMaterialを戻す
	RestorePreviewMaterial();
}

void Engine::ShaderGraphScenePreview::SetTargetEntityUUID(UUID entityUUID) {

	if (previewEntityUUID_ == entityUUID) { return; }
	RestorePreviewMaterial();
	previewEntityUUID_ = entityUUID;
}

void Engine::ShaderGraphScenePreview::SynchronizeWorld(ECSWorld* world) {

	if (!previewMaterialApplied_) { return; }
	// 前のWorldと対象へ編集状態を持ち越さない
	if (!world || appliedWorldLifetime_.lock() != world->GetLifetime() ||
		!world->IsAlive(world->FindByUUID(appliedPreviewEntityUUID_))) {
		RestorePreviewMaterial();
		previewEntityUUID_ = {};
	}
}

bool Engine::ShaderGraphScenePreview::ApplyPreviewMaterial(
	const EditorToolContext& context, ShaderGraphTarget target, AssetID material, std::string& statusMessage) {

	ECSWorld* world = context.GetWorld();
	SynchronizeWorld(world);
	if (!world || !previewEntityUUID_ || !material) { return false; }
	const Entity entity = world->FindByUUID(previewEntityUUID_);
	if (!world->IsAlive(entity)) {
		RestorePreviewMaterial();
		previewEntityUUID_ = {};
		return false;
	}

	// 対象の描画方式が変わったら元のMaterialを戻す
	if (previewMaterialApplied_ && appliedPreviewTarget_ != target) { RestorePreviewMaterial(); }
	if (!previewMaterialApplied_) {
		if (!ReadRendererMaterial(*world, entity, target, previewOriginalMaterial_)) {
			statusMessage = "描画対象に対応するRendererがありません";
			return false;
		}
		appliedPreviewEntityUUID_ = previewEntityUUID_;
		appliedWorld_ = world;
		appliedWorldLifetime_ = world->GetLifetime();
		appliedPreviewTarget_ = target;
		previewMaterialApplied_ = true;
	}
	if (!WriteRendererMaterial(*world, entity, target, material)) {
		RestorePreviewMaterial();
		return false;
	}
	statusMessage = "マテリアルをプレビューしています";
	return true;
}

void Engine::ShaderGraphScenePreview::RestorePreviewMaterial() {

	if (!previewMaterialApplied_) { return; }
	// Worldが生存している間だけ元の値を戻す
	const auto lifetime = appliedWorldLifetime_.lock();
	if (lifetime && lifetime->IsAlive()) {
		const Entity entity = appliedWorld_->FindByUUID(appliedPreviewEntityUUID_);
		if (appliedWorld_->IsAlive(entity)) {
			WriteRendererMaterial(*appliedWorld_, entity, appliedPreviewTarget_, previewOriginalMaterial_, false);
		}
	}
	// 復元先と所有していた状態を解除する
	appliedPreviewEntityUUID_ = {};
	previewOriginalMaterial_ = {};
	appliedWorld_ = nullptr;
	appliedWorldLifetime_.reset();
	previewMaterialApplied_ = false;
}
