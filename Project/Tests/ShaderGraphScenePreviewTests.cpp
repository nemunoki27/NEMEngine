#include "ShaderGraphScenePreviewTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphScenePreview.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// c++
#include <memory>
#include <type_traits>

namespace {

	using namespace Engine;

	template<typename T>
	bool TestRendererTarget(ShaderGraphTarget target) {

		ECSWorld world;
		const Entity entity = world.CreateEntity();
		T& renderer = world.AddComponent<T>(entity);
		if constexpr (std::is_same_v<T, PrimitiveRendererComponent>) {
			renderer.renderSpace = target == ShaderGraphTarget::Primitive2D
				? PrimitiveRenderSpace::Screen2D : PrimitiveRenderSpace::World3D;
		}
		const AssetID original{31, 1}, preview{31, 2};
		renderer.material = original;
		EditorToolContext context;
		context.toolContext.world = &world;
		std::string status;
		{
			ShaderGraphScenePreview session;
			session.SetTargetEntityUUID(world.GetUUID(entity));
			if (!session.ApplyPreviewMaterial(context, target, preview, status) ||
				world.GetComponent<T>(entity).material != preview) { return false; }
			// 描画領域を変更しても元のMaterialを復元する
			if constexpr (std::is_same_v<T, PrimitiveRendererComponent>) {
				world.GetComponent<T>(entity).renderSpace = target == ShaderGraphTarget::Primitive2D
					? PrimitiveRenderSpace::World3D : PrimitiveRenderSpace::Screen2D;
			}
		}
		return world.GetComponent<T>(entity).material == original;
	}

	bool TestTargetAndWorldChanges() {

		const AssetID original{32, 1}, preview{32, 2}, other{32, 3};
		ECSWorld world;
		const Entity first = world.CreateEntity(), second = world.CreateEntity();
		world.AddComponent<MeshRendererComponent>(first).material = original;
		world.AddComponent<MeshRendererComponent>(second).material = other;
		EditorToolContext context;
		context.toolContext.world = &world;
		ShaderGraphScenePreview session;
		std::string status;
		session.SetTargetEntityUUID(world.GetUUID(first));
		if (!session.ApplyPreviewMaterial(context, ShaderGraphTarget::Mesh, preview, status)) { return false; }
		// 対象の変更は前のEntityを先に復元する
		session.SetTargetEntityUUID(world.GetUUID(second));
		if (session.IsMaterialApplied() || world.GetComponent<MeshRendererComponent>(first).material != original ||
			!session.ApplyPreviewMaterial(context, ShaderGraphTarget::Mesh, preview, status)) { return false; }
		// 対応しないRendererへの変更で一時Materialを残さない
		if (session.ApplyPreviewMaterial(context, ShaderGraphTarget::Sprite, preview, status) ||
			session.IsMaterialApplied() || world.GetComponent<MeshRendererComponent>(second).material != other) { return false; }
		if (!session.ApplyPreviewMaterial(context, ShaderGraphTarget::Mesh, preview, status)) { return false; }

		// 同じUUIDの別Worldへプレビューを持ち越さない
		ECSWorld replacement;
		const Entity sameUUID = replacement.CreateEntity(world.GetUUID(second));
		replacement.AddComponent<MeshRendererComponent>(sameUUID).material = original;
		session.SynchronizeWorld(&replacement);
		if (session.IsMaterialApplied() || session.GetTargetEntityUUID() ||
			world.GetComponent<MeshRendererComponent>(second).material != other ||
			replacement.GetComponent<MeshRendererComponent>(sameUUID).material != original) { return false; }
		session.RestorePreviewMaterial();
		return replacement.GetComponent<MeshRendererComponent>(sameUUID).material == original;
	}

	bool TestDestroyedTargets() {

		ShaderGraphScenePreview session;
		std::string status;
		auto world = std::make_unique<ECSWorld>();
		const Entity entity = world->CreateEntity();
		world->AddComponent<MeshRendererComponent>(entity).material = {33, 1};
		EditorToolContext context;
		context.toolContext.world = world.get();
		session.SetTargetEntityUUID(world->GetUUID(entity));
		if (!session.ApplyPreviewMaterial(context, ShaderGraphTarget::Mesh, {33, 2}, status)) { return false; }
		world->DestroyEntity(entity);
		world->FlushPendingDestroyEntities();
		session.SynchronizeWorld(world.get());
		if (session.IsMaterialApplied() || session.GetTargetEntityUUID()) { return false; }
		const Entity next = world->CreateEntity();
		world->AddComponent<MeshRendererComponent>(next).material = {33, 1};
		session.SetTargetEntityUUID(world->GetUUID(next));
		if (!session.ApplyPreviewMaterial(context, ShaderGraphTarget::Mesh, {33, 2}, status)) { return false; }
		// Worldの破棄後は古いポインタを参照しない
		world.reset();
		session.SynchronizeWorld(nullptr);
		return !session.IsMaterialApplied() && !session.GetTargetEntityUUID();
	}
}

bool NEMTests::TestShaderGraphScenePreview() {

	return TestRendererTarget<Engine::MeshRendererComponent>(Engine::ShaderGraphTarget::Mesh) &&
		TestRendererTarget<Engine::PrimitiveRendererComponent>(Engine::ShaderGraphTarget::Primitive3D) &&
		TestRendererTarget<Engine::PrimitiveRendererComponent>(Engine::ShaderGraphTarget::Primitive2D) &&
		TestRendererTarget<Engine::SpriteRendererComponent>(Engine::ShaderGraphTarget::Sprite) &&
		TestRendererTarget<Engine::TextRendererComponent>(Engine::ShaderGraphTarget::Text) &&
		TestTargetAndWorldChanges() && TestDestroyedTargets();
}
