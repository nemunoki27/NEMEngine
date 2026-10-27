#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedMaterialConversion.h"
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <span>
#include <string_view>
#include <variant>

using namespace Engine::ManagedMaterialConversion;

namespace Engine {

	namespace {

		// Instanceの上書きがない値は描画に使うMaterialから取得する
		const MaterialAsset* ResolveRendererMaterial(AssetID id, ManagedRendererMaterialTarget target) {

			const auto* context = ManagedScriptRuntime::GetCurrentContext();
			if (!context || !context->renderAssetLibrary || !context->assetDatabase) return nullptr;
			auto& library = *context->renderAssetLibrary;
			library.Init(context->assetDatabase);
			DefaultMaterialSlot slot = DefaultMaterialSlot::Mesh;
			if (target == ManagedRendererMaterialTarget::Decal) slot = DefaultMaterialSlot::Decal;
			else if (target == ManagedRendererMaterialTarget::Sprite) slot = DefaultMaterialSlot::Sprite;
			else if (target == ManagedRendererMaterialTarget::Text) slot = DefaultMaterialSlot::Text;
			else if (target == ManagedRendererMaterialTarget::Primitive) slot = DefaultMaterialSlot::Primitive;
			else if (target == ManagedRendererMaterialTarget::Line) slot = DefaultMaterialSlot::Line;
			return library.LoadMaterial(MaterialResolver{}.ResolveORDefault(*context->assetDatabase, id, slot));
		}

		// Renderer種別に応じてInstance値を操作
		template<typename Function>
		int32_t VisitRendererMaterialInstances(
			Engine::ECSWorld& world, const Engine::Entity& entity,
			Engine::ManagedRendererMaterialTarget target,
			int32_t subMeshIndex, Function&& function) {

			int32_t count = 0;
			auto visit = [&](Engine::MaterialParameterSet& materialInstance, AssetID material) {
				function(materialInstance, material);
				++count;
			};

			switch (target) {
			case Engine::ManagedRendererMaterialTarget::Mesh: {
				const auto* renderer = world.TryGetComponentForBinding<Engine::MeshRendererComponent>(entity);
				if (!renderer) {
					break;
				}
				if (const std::span<Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
					subMeshIndex < 0) {

					for (Engine::SubMeshMaterial& subMesh : subMeshes) {
						visit(subMesh.materialInstance, subMesh.material ? subMesh.material : renderer->material);
					}
				} else if (static_cast<size_t>(subMeshIndex) < subMeshes.size()) {
					auto& subMesh = subMeshes[static_cast<size_t>(subMeshIndex)];
					visit(subMesh.materialInstance, subMesh.material ? subMesh.material : renderer->material);
				}
				break;
			}
			case Engine::ManagedRendererMaterialTarget::Sprite:
				if (Engine::SpriteRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::SpriteRendererComponent>(entity)) {

					visit(renderer->materialInstance, renderer->material);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Text:
				if (Engine::TextRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::TextRendererComponent>(entity)) {

					visit(renderer->materialInstance, renderer->material);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Primitive:
				if (Engine::PrimitiveRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::PrimitiveRendererComponent>(entity)) {

					auto material = renderer->material;
					const auto* context = ManagedScriptRuntime::GetCurrentContext();
					if (!material && IsPrimitiveScreen2D(*renderer) && context && context->assetDatabase) {
						material = MaterialResolver{}.ResolveORDefault(*context->assetDatabase, material, DefaultMaterialSlot::Primitive2D);
					}
					visit(renderer->materialInstance, material);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Decal:
				if (auto* projector = world.TryGetComponentForBinding<DecalProjectorComponent>(entity)) {
					visit(projector->materialInstance, projector->material);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Line:
				if (Engine::LineRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::LineRendererComponent>(entity)) {

					visit(renderer->materialInstance, renderer->material);
				}
				break;
			}
			return count;
		}

	}

	int32_t ManagedScriptRuntime::SetRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID, const char* name,
		const ManagedMaterialParameterValue* value) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || !value || !name || name[0] == '\0') {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		MaterialParameterValue decoded{};
		if (!DecodeMaterialParameterValue(*value, decoded)) {
			return 0;
		}
		const std::string_view parameterName(name);
		const MaterialParameterID id{
			parameterID != 0 ?
			parameterID : MaterialParameterID::FromName(parameterName).value
		};
		const Color4* color = std::get_if<Color4>(&decoded.value);
		const bool colorOnly = target == static_cast<int32_t>(ManagedRendererMaterialTarget::Mesh) &&
			id == MaterialParameterIDs::BaseColor && parameterName == MaterialParameterNames::BaseColor && color;
		bool changed = false;
		bool rejected = false;
		const int32_t updated = VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance, AssetID materialID) {
				if (colorOnly) {
					const auto* previous = materialInstance.Find(id);
					const auto* oldColor = previous ? std::get_if<Color4>(&previous->value) : nullptr;
					if (oldColor && oldColor->r == color->r && oldColor->g == color->g &&
						oldColor->b == color->b && oldColor->a == color->a) {
						return;
					}
				}
				const auto* material = ResolveRendererMaterial(materialID, static_cast<ManagedRendererMaterialTarget>(target));
				const auto resolvedID = material ? ResolveParameterID(material->parameters, id) : ResolveParameterID(materialInstance, id);
				const auto* previous = FindParameter(materialInstance, resolvedID);
				if (!previous && material) previous = FindParameter(material->parameters, resolvedID);
				if (!IsParameterTypeCompatible(previous, decoded)) { rejected = true; return; }
				materialInstance.Set(resolvedID, parameterName, ResolveMaterialParameterSemantic(parameterName), decoded);
				changed = true;
			});
		// 色だけの変更と描画構成の変更を分けて通知
		if (changed) {
			if (colorOnly) {
				world->MarkMeshColorModified(resolved);
			} else if (target != static_cast<int32_t>(ManagedRendererMaterialTarget::Decal)) {
				world->MarkRenderDataModified(resolved);
			}
		}
		return rejected ? 0 : updated;
	}

	int32_t ManagedScriptRuntime::GetRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID, ManagedMaterialParameterValue* outValue) {

		if (!outValue || parameterID == 0) {
			return 0;
		}
		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		bool found = false;
		VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target),
			subMeshIndex < 0 ? 0 : subMeshIndex,
			[&](MaterialParameterSet& materialInstance, AssetID materialID) {
				if (found) {
					return;
				}
				const MaterialParameterSet& readOnly = materialInstance;
				const MaterialParameterValue* value = FindParameter(readOnly, MaterialParameterID{parameterID});
				if (!value) {
					const auto* material = ResolveRendererMaterial(materialID, static_cast<ManagedRendererMaterialTarget>(target));
					value = material ? FindParameter(material->parameters, MaterialParameterID{parameterID}) : nullptr;
				}
				found = value && EncodeMaterialParameterValue(*value, *outValue);
			});
		return found ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ClearRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || parameterID == 0) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		int32_t removed = 0;
		VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance, [[maybe_unused]] AssetID materialID) {
				removed += static_cast<int32_t>(materialInstance.erase(ResolveParameterID(materialInstance, MaterialParameterID{parameterID})));
			});
		if (removed != 0 && target != static_cast<int32_t>(ManagedRendererMaterialTarget::Decal)) {
			world->MarkRenderDataModified();
		}
		return removed;
	}
}
