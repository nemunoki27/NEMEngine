#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"
#include "ManagedMaterialConversion.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
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

		template<typename Function>
		int32_t VisitRendererMaterialInstances(
			Engine::ECSWorld& world, const Engine::Entity& entity,
			Engine::ManagedRendererMaterialTarget target,
			int32_t subMeshIndex, Function&& function) {

			int32_t count = 0;
			auto visit = [&](Engine::MaterialParameterSet& materialInstance) {
				function(materialInstance);
				++count;
			};

			switch (target) {
			case Engine::ManagedRendererMaterialTarget::Mesh:
				if (!world.TryGetComponentForBinding<Engine::MeshRendererComponent>(entity)) {
					break;
				}
				if (const std::span<Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
					subMeshIndex < 0) {

					for (Engine::SubMeshMaterial& subMesh : subMeshes) {
						visit(subMesh.materialInstance);
					}
				} else if (static_cast<size_t>(subMeshIndex) < subMeshes.size()) {
					visit(subMeshes[static_cast<size_t>(subMeshIndex)].materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Sprite:
				if (Engine::SpriteRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::SpriteRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Text:
				if (Engine::TextRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::TextRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Primitive:
				if (Engine::PrimitiveRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::PrimitiveRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Line:
				if (Engine::LineRendererComponent* renderer =
					world.TryGetComponentForBinding<Engine::LineRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			}
			return count;
		}

		// CollisionComponentの単一形状を取得する


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
		const int32_t updated = VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance) {
				if (colorOnly) {
					const auto* previous = materialInstance.Find(id);
					const auto* oldColor = previous ? std::get_if<Color4>(&previous->value) : nullptr;
					if (oldColor && oldColor->r == color->r && oldColor->g == color->g &&
						oldColor->b == color->b && oldColor->a == color->a) {
						return;
					}
				}
				materialInstance.Set(
					id, parameterName,
					ResolveMaterialParameterSemantic(parameterName), decoded);
				changed = true;
			});
		if (changed) {
			if (colorOnly) {
				world->MarkMeshColorModified(resolved);
			} else {
				world->MarkRenderDataModified(resolved);
			}
		}
		return updated;
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
			[&](MaterialParameterSet& materialInstance) {
				if (found) {
					return;
				}
				const MaterialParameterSet& readOnly =
					materialInstance;
				const MaterialParameterValue* value =
					readOnly.Find(MaterialParameterID{ parameterID });
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
			[&](MaterialParameterSet& materialInstance) {
				removed += static_cast<int32_t>(
					materialInstance.erase(MaterialParameterID{ parameterID }));
			});
		if (removed != 0) {
			world->MarkRenderDataModified();
		}
		return removed;
	}
}
