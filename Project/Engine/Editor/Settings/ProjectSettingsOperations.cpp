#include "ProjectSettingsOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/RemapEntityTagsCommand.h>
#include <Engine/Editor/Commands/Entity/ClearRenderingLayerCommand.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneStorageJournal.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <functional>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

	struct ProjectJsonDocument {
		std::filesystem::path path;
		nlohmann::json data;
	};

	template <typename Fn>
	bool UpdateScenePrefabDocuments(const Engine::EditorContext& context,
		Engine::AssetDatabase& database, Fn&& update) {

		std::vector<ProjectJsonDocument> documents;
		for (const auto& [assetID, meta] : database.GetAssets()) {
			if (meta.type != Engine::AssetType::Scene &&
				meta.type != Engine::AssetType::Prefab) {
				continue;
			}
			// 開いている文書はWorld側のCommandだけを更新する
			if ((context.sceneInstances &&
				!context.sceneInstances->FindInstanceIDs(assetID).empty()) ||
				(context.isPrefabEditing && context.prefabEditAsset == assetID)) {
				continue;
			}
			const std::filesystem::path path = database.ResolveFullPath(assetID);
			if (path.empty()) {
				continue;
			}
			ProjectJsonDocument document{ path, {} };
			if (!Engine::JsonFile::TryLoad(path, document.data)) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
					"Project設定の参照更新を読み込めません path={}", path.string());
				return false;
			}
			documents.emplace_back(std::move(document));
		}

		std::vector<ProjectJsonDocument*> changed;
		for (ProjectJsonDocument& document : documents) {
			if (update(document.data)) {
				changed.emplace_back(&document);
			}
		}
		std::vector<Engine::SceneStorageChange> changes;
		changes.reserve(changed.size());
		for (ProjectJsonDocument* document : changed) {
			changes.push_back({ document->path, document->data });
		}
		std::string error;
		const auto recover = [](const std::filesystem::path& directory,
			std::string& recoveryError) {

			return Engine::SceneStorageJournal::Recover(
				directory, recoveryError, [](const std::filesystem::path&) {});
		};
		if (!Engine::SceneStorageJournal::Commit(
			changes, "Project設定の参照更新", error, recover)) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"Project設定の参照更新を保存できません 内容={}", error);
			return false;
		}
		if (!changed.empty()) {
			database.RebuildMeta();
		}
		return true;
	}

	bool ReplaceSceneTags(nlohmann::json& root,
		const std::string& from, const std::string& to) {

		bool changed = false;
		if (!root.contains("Entities") || !root["Entities"].is_array()) {
			return false;
		}
		for (nlohmann::json& entity : root["Entities"]) {
			if (!entity.is_object() || !entity.contains("Components") ||
				!entity["Components"].is_object()) {
				continue;
			}
			nlohmann::json& sceneObject = entity["Components"]["SceneObject"];
			if (sceneObject.is_object() && sceneObject.value("tag", "") == from) {
				sceneObject["tag"] = to;
				changed = true;
			}
		}
		return changed;
	}

	bool ClearLayerMasks(nlohmann::json& value, uint32_t layerIndex) {

		if (layerIndex >= 32u) {
			return false;
		}
		bool changed = false;
		const uint32_t bit = 1u << layerIndex;
		if (value.is_object()) {
			for (auto& [key, child] : value.items()) {
				if (key == "renderingLayerMask") {
					if (!child.is_number_unsigned() && !child.is_number_integer()) {
						continue;
					}
					const int64_t raw = child.get<int64_t>();
					if (raw < 0 || raw > 0xFFFFFFFFll) {
						continue;
					}
					const uint32_t mask = static_cast<uint32_t>(raw);
					const uint32_t next = mask & ~bit;
					if (next != mask) {
						child = next;
						changed = true;
					}
				} else {
					changed |= ClearLayerMasks(child, layerIndex);
				}
			}
		} else if (value.is_array()) {
			for (nlohmann::json& child : value) {
				changed |= ClearLayerMasks(child, layerIndex);
			}
		}
		return changed;
	}

	uint32_t RemoveMaskBit(uint32_t mask, uint32_t index) {

		const uint32_t lower = index == 0 ? 0u : mask & ((1u << index) - 1u);
		const uint32_t upper = index == 31 ? 0u : (mask >> (index + 1u)) << index;
		return lower | upper;
	}

	bool IsFieldModule(const std::string& id) {

		return id == "ForceField" || id == "VectorField" || id == "Collision";
	}

	bool RemapMask(nlohmann::json& object, const char* key, uint32_t index) {

		auto found = object.find(key);
		if (found == object.end() || !found->is_number_integer()) return false;
		const auto mask = found->get<uint32_t>();
		if (mask == UINT32_MAX) return false;
		const auto next = RemoveMaskBit(mask, index);
		if (next == mask) return false;
		*found = next;
		return true;
	}

	bool RemoveCollisionTypeMasks(nlohmann::json& value, uint32_t typeIndex) {

		bool changed = false;
		if (value.is_object()) {
			for (auto& [key, child] : value.items()) {
				if (key == "ForceField" || key == "VectorField" || key == "HeightmapCollider") {
					changed |= RemapMask(child, "layerMask", typeIndex);
				} else if (key == "Cloth") {
					changed |= RemapMask(child, "windCollisionMask", typeIndex);
					changed |= RemapMask(child, "collisionMask", typeIndex);
				} else if (key == "params" && value.contains("id") && value["id"].is_string() && IsFieldModule(value["id"].get<std::string>())) {
					changed |= RemapMask(child, "layerMask", typeIndex);
				} else if (key == "typeMask" && (child.is_number_unsigned() || child.is_number_integer())) {
					const uint32_t mask = child.get<uint32_t>();
					const uint32_t next = RemoveMaskBit(mask, typeIndex);
					if (next != mask) {
						child = next;
						changed = true;
					}
				} else {
					changed |= RemoveCollisionTypeMasks(child, typeIndex);
				}
			}
		} else if (value.is_array()) {
			for (nlohmann::json& child : value) {
				changed |= RemoveCollisionTypeMasks(child, typeIndex);
			}
		}
		return changed;
	}
}

bool Engine::ProjectSettingsOperations::RemapTags(const EditorToolContext& context, const std::string& from, const std::string& to) {

	// 開いているシーンのタグ文字列だけ付け替える、コマンド経由でUndoできる
	if (!context.CanEditScene() || !context.panelContext ||
		!context.panelContext->host || !context.panelContext->editorContext) {
		return false;
	}
	const bool worldChanged = context.panelContext->host->ExecuteEditorCommand(
		std::make_unique<RemapEntityTagsCommand>(from, to));
	if (context.panelContext->editorContext->assetDatabase) {
		if (!UpdateScenePrefabDocuments(*context.panelContext->editorContext,
			*context.panelContext->editorContext->assetDatabase,
			[&](nlohmann::json& root) { return ReplaceSceneTags(root, from, to); })) {

			// Project文書の保存に失敗したためWorld側も元へ戻す
			if (worldChanged) {
				context.panelContext->host->UndoEditorCommand();
			}
			return false;
		}
	}
	return true;
}

bool Engine::ProjectSettingsOperations::ClearRenderingLayer(
	const EditorToolContext& context, uint32_t layerIndex) {

	if (!context.CanEditScene() || !context.panelContext ||
		!context.panelContext->host || !context.panelContext->editorContext) {
		return false;
	}
	const bool worldChanged = context.panelContext->host->ExecuteEditorCommand(
		std::make_unique<ClearRenderingLayerCommand>(layerIndex));
	if (context.panelContext->editorContext->assetDatabase) {
		if (!UpdateScenePrefabDocuments(*context.panelContext->editorContext,
			*context.panelContext->editorContext->assetDatabase,
			[layerIndex](nlohmann::json& root) {
				return ClearLayerMasks(root, layerIndex);
			})) {

			// Project文書の保存に失敗したためWorld側も元へ戻す
			if (worldChanged) {
				context.panelContext->host->UndoEditorCommand();
			}
			return false;
		}
	}
	return true;
}

bool Engine::ProjectSettingsOperations::RemoveCollisionType(
	const EditorToolContext& context, uint32_t typeIndex) {

	if (typeIndex >= 32u || !context.panelContext ||
		!context.panelContext->editorContext) {
		return false;
	}
	const EditorContext& editorContext = *context.panelContext->editorContext;
	if (editorContext.assetDatabase && !UpdateScenePrefabDocuments(
		editorContext, *editorContext.assetDatabase,
		[typeIndex](nlohmann::json& root) {
			return RemoveCollisionTypeMasks(root, typeIndex);
			})) {
		return false;
	}
	if (ECSWorld* world = context.GetWorld()) {
		const auto remap = [typeIndex](uint32_t mask) { return mask == UINT32_MAX ? mask : RemoveMaskBit(mask, typeIndex); };
		const auto remapField = [&]<class T>() {
			world->ForEach<T>([&](Entity owner, T& field) {
				const auto next = remap(field.layerMask);
				if (field.layerMask == next) return;
				field.layerMask = next;
				world->MarkComponentModified<T>(owner);
			});
		};
		remapField.operator()<ForceFieldComponent>();
		remapField.operator()<VectorFieldComponent>();
		remapField.operator()<HeightmapColliderComponent>();
		world->ForEach<ClothComponent>([&](Entity owner, ClothComponent&) {
			const auto* record = TryGetCloth(*world, owner);
			if (!record) return;
			auto settings = record->settings;
			settings.windCollisionMask = remap(settings.windCollisionMask);
			settings.collisionMask = remap(settings.collisionMask);
			if (settings.windCollisionMask != record->settings.windCollisionMask || settings.collisionMask != record->settings.collisionMask)
				SetClothSettings(*world, owner, settings);
		});
		world->ForEach<ParticleSystemComponent>([&](Entity owner, ParticleSystemComponent&) {
			const auto source = GetParticleSystemSettings(*world, owner);
			if (!source) return;
			auto settings = *source;
			bool changed = false;
			for (auto& phase : settings.phases) {
				for (auto& module : phase.modules) {
					if (IsFieldModule(module.id)) changed |= RemapMask(module.params, "layerMask", typeIndex);
				}
			}
			if (changed) SetParticleSystemSettings(*world, owner, settings);
		});
		world->ForEach<CollisionComponent>([typeIndex](Entity, CollisionComponent& collision) {
			collision.typeMask = RemoveMaskBit(collision.typeMask, typeIndex);
			});
	}
	return true;
}
