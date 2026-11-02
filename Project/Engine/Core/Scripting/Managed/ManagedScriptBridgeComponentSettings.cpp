#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedCameraSettings.h"
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

using namespace Engine;
using namespace Engine::ManagedCameraSettings;

//============================================================================
//	ManagedScriptRuntime componentSettingsMethods
//============================================================================
int32_t ManagedScriptRuntime::CopyComponentSettingsCallback(ManagedNativeEntity entity, int32_t type,
	char* buffer, int32_t capacity) {

	try {
		auto* world = ResolveWorld(entity);
		const auto target = ResolveEntity(entity);
		if (!world || !world->IsAlive(target)) return -1;
		nlohmann::json data;
		if (type == 0) {
			const auto* component = world->TryGetComponentForBinding<AnimationClipPlayerComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 14) {
			const auto* component = world->TryGetComponentForBinding<MeshRendererComponent>(target);
			if (!component) return -1;
			SerializeMeshRenderer(*component, GetMeshSubMeshes(*world, target), data);
		} else if (type == 16 || type == 17) {
			const auto* camera = ResolveCamera(*world, target, type);
			const auto* context = GetCurrentContext();
			if (!camera || !context || !context->renderAssetLibrary || !context->assetDatabase) return -1;
			auto& library = *context->renderAssetLibrary;
			library.Init(context->assetDatabase);
			data = {{"reflectionParameters", camera->reflectionParameters}};
			data["cameraInstanceID"] = world->GetBindingComponentInstanceID(target, type);
			data["renderPassesAsset"] = ToString(camera->renderPasses);
			data["renderPassesRevision"] = library.GetRenderPassesRevision(camera->renderPasses);
			if (const auto* passes = ResolvePasses(*camera, library)) data["renderPasses"] = ToJson(*passes);
		} else if (type == 70) {
			const auto* component = world->TryGetComponentForBinding<SimpleAnimationComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 35) {
			const auto* component = world->TryGetComponentForBinding<UISelectableComponent>(target);
			if (!component) return -1;
			data = *component;
		} else return -1;
		return CopyStringToBuffer(data.dump(), buffer, capacity);
	} catch (...) { return -1; }
}

int32_t ManagedScriptRuntime::ApplyComponentSettingsCallback(ManagedNativeEntity entity, int32_t type,
	const char* buffer, int32_t size) {

	if (!buffer || size <= 0 || size > 16 * 1024 * 1024) return 0;
	try {
		auto* world = ResolveWorld(entity);
		const auto target = ResolveEntity(entity);
		if (!world || !world->IsAlive(target)) return 0;
		const auto data = nlohmann::json::parse(buffer, buffer + size);
		if (type == 70) {
			auto* component = world->TryGetComponentForBinding<SimpleAnimationComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			if (data.contains("targetNative")) {
				const auto& value = data.at("targetNative");
				ManagedNativeEntity reference{};
				reference.world.index = value.at("world").at("index").get<uint32_t>();
				reference.world.generation = value.at("world").at("generation").get<uint32_t>();
				reference.index = value.at("index").get<uint32_t>();
				reference.generation = value.at("generation").get<uint32_t>();
				if (reference.IsValid()) {
					if (ResolveWorld(reference) != world || !world->IsAlive(ResolveEntity(reference))) return 0;
					candidate.runtimeTarget = ResolveEntity(reference);
					candidate.runtimeTargetOverride = true;
					candidate.target = SceneEntityReference::FromEntity(*world, candidate.runtimeTarget);
				} else {
					candidate.target = {}; candidate.runtimeTargetOverride = false;
				}
				candidate.binding = data.at("binding").get<AnimationPropertyBinding>();
			} else {
				from_json(data, candidate);
			}
			*component = std::move(candidate);
			world->MarkComponentModified<SimpleAnimationComponent>(target);
			return 1;
		}
		if (type == 35) {
			auto* component = world->TryGetComponentForBinding<UISelectableComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			from_json(data, candidate);
			ApplyUISelectableAuthoring(candidate, *component);
			world->MarkComponentModified<UISelectableComponent>(target);
			return 1;
		}
		if (type == 0) {
			auto* component = world->TryGetComponentForBinding<AnimationClipPlayerComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			from_json(data, candidate);
			ApplyAnimationClipPlayerSettings(candidate, *component);
			world->MarkComponentModified<AnimationClipPlayerComponent>(target);
			return 1;
		}
		if (type == 16 || type == 17) {
			auto* camera = ResolveCamera(*world, target, type);
			const auto* context = GetCurrentContext();
			if (!camera || !context || !context->renderAssetLibrary) return 0;
			// 別のCameraや再読込前のSnapshotを適用しない
			if (data.value("cameraInstanceID", uint64_t(0)) != world->GetBindingComponentInstanceID(target, type) ||
				ParseAssetID(data, "renderPassesAsset") != camera->renderPasses ||
				data.value("renderPassesRevision", uint64_t(0)) !=
				context->renderAssetLibrary->GetRenderPassesRevision(camera->renderPasses)) return 0;
			std::shared_ptr<const RenderPassesAsset> passes = camera->runtimeRenderPasses;
			if (data.contains("renderPasses")) {
				RenderPassesAsset candidate;
				if (!FromJson(data.at("renderPasses"), candidate)) return 0;
				std::unordered_set<uint64_t> ids;
				for (const auto& pass : candidate.passes) {
					if (!pass.id.value || !ids.insert(pass.id.value).second) return 0;
				}
				candidate.guid = camera->renderPasses;
				if (!candidate.guid) return 0;
				passes = std::make_shared<const RenderPassesAsset>(std::move(candidate));
			}
			if (data.contains("reflectionParameters") && !data["reflectionParameters"].is_object()) return 0;
			// 全項目の変換成功後にCamera固有の設定を公開する
			camera->runtimeRenderPasses = std::move(passes);
			camera->runtimeRenderPassesSourceRevision = context->renderAssetLibrary->GetRenderPassesRevision(camera->renderPasses);
			if (data.contains("reflectionParameters")) camera->reflectionParameters = data["reflectionParameters"];
			world->MarkComponentModified(target, type);
			return 1;
		}
		if (type == 14) {
			if (!world->HasComponent<MeshRendererComponent>(target)) return 0;
			const auto subMeshes = GetMeshSubMeshes(*world, target);
			const auto index = data.at("index").get<uint32_t>();
			if (index >= subMeshes.size()) return 0;
			SubMeshMaterial candidate = data.at("value").get<SubMeshMaterial>();
			if (candidate.stableID != subMeshes[index].stableID) return 0;
			// 読取専用のモデル由来情報は現在の値を維持する
			candidate.sourceSubMeshIndex = subMeshes[index].sourceSubMeshIndex;
			candidate.sourceSurfaceMode = subMeshes[index].sourceSurfaceMode;
			candidate.sourcePivot = subMeshes[index].sourcePivot;
			return SetMeshSubMesh(*world, target, index, candidate);
		}
		return 0;
	} catch (...) { return 0; }
}
