#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedCameraSettings.h"
#include "ManagedMaterialConversion.h"
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Renderer/Views/CameraViewSelection.h>

// c++
#include <algorithm>
#include <functional>

using namespace Engine;
using namespace Engine::ManagedMaterialConversion;
using namespace Engine::ManagedCameraSettings;

namespace {

	// ComponentとAssetの両方の世代が一致するか確認する
	CameraCommon* Validate(const ManagedRenderPassHandle& handle, ECSWorld*& world, RenderAssetLibrary*& library) {

		const auto* context = ManagedScriptRuntime::GetCurrentContext();
		world = ResolveWorld(handle.camera);
		library = context ? context->renderAssetLibrary : nullptr;
		if (!world || !library || !world->IsAlive(ResolveEntity(handle.camera)) || handle.cameraType < 0 ||
			world->GetBindingComponentInstanceID(ResolveEntity(handle.camera), handle.cameraType) != handle.instanceID) return nullptr;
		auto* camera = ResolveCamera(*world, ResolveEntity(handle.camera), handle.cameraType);
		return camera && camera->renderPasses == ToAssetID(handle.profile) &&
			library->GetRenderPassesRevision(camera->renderPasses) == handle.revision ? camera : nullptr;
	}

	// 同じAssetを使うCameraにも変更が漏れないよう複製する
	void Publish(CameraCommon& camera, ECSWorld& world, const ManagedRenderPassHandle& handle, RenderPassesAsset settings) {

		camera.runtimeRenderPasses = std::make_shared<const RenderPassesAsset>(std::move(settings));
		camera.runtimeRenderPassesSourceRevision = handle.revision;
		world.MarkComponentModified(ResolveEntity(handle.camera), handle.cameraType);
	}

	// 設定内のPassをIDから検索する
	template<class T> auto FindCameraSettingsPass(T& settings, uint64_t id) {

		return std::find_if(settings.passes.begin(), settings.passes.end(), [id](const auto& pass) { return pass.id.value == id; });
	}
}

//============================================================================
//	ManagedScriptRuntime cameraPassMethods
//============================================================================
int32_t ManagedScriptRuntime::ResolveCameraRenderPassCallback(ManagedNativeEntity entity, int32_t type,
	const char* name, ManagedRenderPassHandle* output) {

	if (!name || !output) return 0;
	try {
		const auto* context = GetCurrentContext();
		if (!context || !context->world || !context->renderAssetLibrary || !context->assetDatabase) return 0;
		context->renderAssetLibrary->Init(context->assetDatabase);
		ECSWorld* world = type < 0 ? context->world : ResolveWorld(entity);
		if (!world) return 0;
		if (type < 0) {
			// 静的APIは実際のMain Cameraと同じ選別を使用する
			auto camera = CameraViewSelection::ResolveBestPerspectiveCamera(*world, 1, 1);
			type = 17;
			if (!camera.valid) { camera = CameraViewSelection::ResolveBestOrthographicCamera(*world, 1, 1); type = 16; }
			if (!camera.valid) return 0;
			entity = MakeNativeEntity(*world, camera.sourceCamera);
		}
		auto* camera = ResolveCamera(*world, ResolveEntity(entity), type);
		if (!camera) return 0;
		auto& library = *context->renderAssetLibrary;
		const auto* settings = ResolvePasses(*camera, library);
		if (!settings) return 0;
		const auto pass = std::find_if(settings->passes.begin(), settings->passes.end(),
			[name](const auto& value) { return value.name == name; });
		if (pass == settings->passes.end() && name[0] != '\0') return 0;
		*output = {entity, type, 0, world->GetBindingComponentInstanceID(ResolveEntity(entity), type),
			ToManagedAssetGUID(camera->renderPasses), library.GetRenderPassesRevision(camera->renderPasses),
			pass == settings->passes.end() ? 0 : pass->id.value};
		return 1;
	} catch (...) { return 0; }
}

int32_t ManagedScriptRuntime::GetCameraRenderPassParameterCallback(const ManagedRenderPassHandle* handle,
	uint64_t parameterID, ManagedMaterialParameterValue* output) {

	if (!handle || !output) return 0;
	try {
		ECSWorld* world; RenderAssetLibrary* library;
		const auto* camera = Validate(*handle, world, library);
		const auto* settings = camera ? ResolvePasses(*camera, *library) : nullptr;
		if (!settings) return 0;
		const auto pass = FindCameraSettingsPass(*settings, handle->passID);
		if (pass == settings->passes.end()) return 0;
		const auto* value = FindParameter(pass->parameterOverrides, MaterialParameterID{parameterID});
		for (const auto& [name, texture] : pass->textureOverrides) {
			if (MaterialParameterID::FromName(name).value == parameterID) {
				MaterialParameterValue textureValue; textureValue.value = texture;
				return EncodeMaterialParameterValue(textureValue, *output);
			}
		}
		// 上書きがない場合はMaterialの保存値を返す
		if (!value) {
			const auto* material = library->LoadMaterial(pass->material);
			value = material ? FindParameter(material->parameters, MaterialParameterID{parameterID}) : nullptr;
		}
		return value && EncodeMaterialParameterValue(*value, *output) ? 1 : 0;
	} catch (...) { return 0; }
}

int32_t ManagedScriptRuntime::SetCameraRenderPassParameterCallback(const ManagedRenderPassHandle* handle,
	uint64_t parameterID, const char* name, const ManagedMaterialParameterValue* value) {

	if (!handle || !name || !value || !parameterID) return 0;
	try {
		ECSWorld* world; RenderAssetLibrary* library;
		auto* camera = Validate(*handle, world, library);
		const auto* source = camera ? ResolvePasses(*camera, *library) : nullptr;
		if (!source) return 0;
		auto settings = *source;
		auto pass = FindCameraSettingsPass(settings, handle->passID);
		MaterialParameterValue decoded;
		if (pass == settings.passes.end() || !DecodeMaterialParameterValue(*value, decoded)) return 0;
		auto id = ResolveParameterID(pass->parameterOverrides, MaterialParameterID{parameterID});
		const auto* previous = pass->parameterOverrides.Find(id);
		if (const auto* material = library->LoadMaterial(pass->material); !previous && material) {
			id = ResolveParameterID(material->parameters, id);
			previous = material->parameters.Find(id);
		}
		if (!IsParameterTypeCompatible(previous, decoded)) return 0;
		pass->parameterOverrides.Set(id, name, MaterialParameterSemantic::None, decoded);
		if (const auto* texture = std::get_if<AssetID>(&decoded.value)) pass->textureOverrides[name] = *texture;
		Publish(*camera, *world, *handle, std::move(settings));
		return 1;
	} catch (...) { return 0; }
}

int32_t ManagedScriptRuntime::CameraRenderPassActionCallback(const ManagedRenderPassHandle* handle,
	int32_t action, const char* name, uint64_t argument) {

	if (!handle) return 0;
	try {
		ECSWorld* world; RenderAssetLibrary* library;
		auto* camera = Validate(*handle, world, library);
		const auto* source = camera ? ResolvePasses(*camera, *library) : nullptr;
		if (!source) return 0;
		const auto current = FindCameraSettingsPass(*source, handle->passID);
		if (action == 0) return !handle->passID || current != source->passes.end();
		if (action == 2) return current != source->passes.end() && current->enabled;
		auto settings = *source;
		auto pass = FindCameraSettingsPass(settings, handle->passID);
		if (action == 1) {
			if (pass == settings.passes.end()) return 0;
			pass->enabled = argument != 0;
		} else if (action == 3) {
			if (!name) return 0;
			bool found = false;
			std::function<void(std::vector<RenderFeatureHierarchyItem>&)> visit = [&](auto& items) {
				for (auto& item : items) {
					if (item.name == name) { item.enabled = argument != 0; found = true; }
					visit(item.children);
				}
			};
			visit(settings.hierarchy);
			if (!found) return 0;
		} else if (action == 5 || action == 6) {
			if (pass == settings.passes.end()) return 0;
			const auto* baseline = library->LoadRenderPasses(camera->renderPasses);
			if (!baseline) return 0;
			const auto saved = FindCameraSettingsPass(*baseline, handle->passID);
			if (saved == baseline->passes.end()) return 0;
			if (action == 6) *pass = *saved;
			else {
				const auto id = ResolveParameterID(pass->parameterOverrides, MaterialParameterID{argument});
				const auto record = pass->parameterOverrides.Find(id);
				if (record) {
					for (const auto& value : pass->parameterOverrides.GetRecords()) {
						if (value.id != id) continue;
						const auto& key = value.namedValue.first;
						pass->textureOverrides.erase(key);
						if (const auto savedTexture = saved->textureOverrides.find(key); savedTexture != saved->textureOverrides.end())
							pass->textureOverrides[key] = savedTexture->second;
					}
				}
				pass->parameterOverrides.erase(id);
				for (const auto& value : saved->parameterOverrides.GetRecords()) {
					if (value.id == id) pass->parameterOverrides.Set(id, value.namedValue.first, value.semantic, value.namedValue.second);
				}
			}
		} else if (action == 7) {
			camera->runtimeRenderPasses.reset();
			world->MarkComponentModified(ResolveEntity(handle->camera), handle->cameraType);
			return 1;
		} else if (action == 8) {
			settings.gameView = (argument & 1) != 0; settings.sceneView = (argument & 2) != 0;
		} else return 0;
		Publish(*camera, *world, *handle, std::move(settings));
		return 1;
	} catch (...) { return 0; }
}
