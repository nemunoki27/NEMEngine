#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedCameraSettings.h"
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Systems/Animation/AnimationGroupLookup.h>

// c++
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

using namespace Engine;
using namespace Engine::ManagedCameraSettings;

namespace {

	// 同じStateの再生時刻を残し、追加と削除を実行配列へ反映する
	void SynchronizeAnimationStates(AnimationPlayerComponent& player, std::string& name,
		std::vector<AnimationClipRuntime>& runtime) {

		const auto* group = AnimationGroupLookup::FindGroup(player, name);
		if (!group) { name.clear(); runtime.clear(); return; }
		std::vector<AnimationClipRuntime> next;
		next.reserve(group->states.size());
		for (const auto& state : group->states) {
			const auto found = std::find_if(runtime.begin(), runtime.end(),
				[&](const auto& value) { return value.stateName == state.name; });
			if (found != runtime.end()) { next.push_back(*found); continue; }
			AnimationClipRuntime value{};
			value.stateName = state.name;
			value.dir = 1;
			value.delayRemaining = (std::max)(state.startDelay, 0.0f);
			value.started = value.delayRemaining <= 0.0f;
			value.playing = value.started;
			next.push_back(std::move(value));
		}
		runtime = std::move(next);
	}
}

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
			const auto* component = world->TryGetComponentForBinding<AnimationPlayerComponent>(target);
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
		if (type == 0) {
			auto* component = world->TryGetComponentForBinding<AnimationPlayerComponent>(target);
			if (!component || !data.contains("groups") || !data["groups"].is_array()) return 0;
			std::vector<AnimationGroup> groups;
			std::unordered_set<std::string> names;
			for (const auto& json : data["groups"]) {
				auto group = LoadAnimationGroup(json);
				if (group.name.empty() || !names.insert(group.name).second) return 0;
				std::unordered_set<std::string> stateNames;
				for (const auto& state : group.states) {
					if (state.name.empty() || !stateNames.insert(state.name).second ||
						!std::isfinite(state.speed) || !std::isfinite(state.weight)) return 0;
				}
				groups.push_back(std::move(group));
			}
			// 全設定の同期に成功してから実行中Componentへ公開する
			auto candidate = *component;
			candidate.groups = std::move(groups);
			SynchronizeAnimationStates(candidate, candidate.runtimeCurrentGroup, candidate.runtimeCurrentClips);
			SynchronizeAnimationStates(candidate, candidate.runtimeFromGroup, candidate.runtimeFromClips);
			if (candidate.runtimeCurrentGroup.empty()) candidate.runtimePlaying = false;
			if (candidate.runtimeFromGroup.empty()) candidate.runtimeInTransition = false;
			candidate.runtimeClipRevision = std::numeric_limits<uint64_t>::max();
			*component = std::move(candidate);
			world->MarkComponentModified<AnimationPlayerComponent>(target);
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
