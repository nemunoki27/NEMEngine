#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"
#include "ManagedMaterialConversion.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <string_view>

using namespace Engine::ManagedMaterialConversion;

namespace Engine {

	namespace {

		const Engine::RenderFeaturePassSettings* ResolveRenderFeaturePass(
			uint64_t passID, uint64_t generation) {

			Engine::RenderFeatureProfileService& service =
				Engine::RenderFeatureProfileService::GetInstance();
			service.EnsureLoaded();
			if (generation == 0 ||
				generation != service.GetRuntimeGeneration()) {

				return nullptr;
			}
			return service.FindPassByID(Engine::UUID{ passID });
		}
	}

	int32_t ManagedScriptRuntime::IsRayTracingSupportedCallback() {

		const SystemContext* context = GetCurrentContext();
		return context && context->graphicsPlatform &&
			context->graphicsPlatform->GetFeatureController().
			GetSupport().SupportsRayTracingPath() ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::IsRayTracingActiveCallback() {

		const SystemContext* context = GetCurrentContext();
		return context && context->graphicsPlatform &&
			context->graphicsPlatform->ShouldUseDispatchRays() ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ResolveRenderFeaturePassCallback(
		const char* passName, uint64_t* outPassID,
		uint64_t* outGeneration) {

		if (!passName || passName[0] == '\0' || !outPassID ||
			!outGeneration) {

			return 0;
		}
		RenderFeatureProfileService& service =
			RenderFeatureProfileService::GetInstance();
		service.EnsureLoaded();
		const RenderFeaturePassSettings* pass =
			service.FindPassByName(passName);
		if (!pass) {
			return 0;
		}
		*outPassID = pass->id.value;
		*outGeneration = service.GetRuntimeGeneration();
		return 1;
	}

	int32_t ManagedScriptRuntime::ValidateRenderFeaturePassCallback(
		uint64_t passID, uint64_t generation) {

		return ResolveRenderFeaturePass(passID, generation) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassEnabledCallback(
		uint64_t passID, uint64_t generation, int32_t enabled) {

		return ResolveRenderFeaturePass(passID, generation) &&
			RenderFeatureRuntimeOverrides::GetInstance().SetEnabled(
				UUID{ passID }, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassSceneColorOutputCallback(
		uint64_t passID, uint64_t generation, int32_t enabled) {

		if (!ResolveRenderFeaturePass(passID, generation)) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] SceneColor出力のパスハンドルが無効です");
			return 0;
		}
		return RenderFeatureRuntimeOverrides::GetInstance().SetSceneColorOutput(
			RenderFeatureProfileService::GetInstance().GetRuntime().GetProfile(),
			UUID{ passID }, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeatureGroupEnabledCallback(
		const char* groupName, int32_t enabled) {

		return groupName && RenderFeatureRuntimeOverrides::GetInstance().
			SetGroupEnabled(groupName, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID,
		const char* parameterName,
		const ManagedMaterialParameterValue* value) {

		if (!ResolveRenderFeaturePass(passID, generation) ||
			!parameterName || !value || parameterID == 0) {

			return 0;
		}
		MaterialParameterValue decoded{};
		if (!DecodeMaterialParameterValue(*value, decoded)) {
			return 0;
		}
		return RenderFeatureRuntimeOverrides::GetInstance().SetParameter(
			UUID{ passID }, MaterialParameterID{ parameterID },
			parameterName, decoded) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID,
		ManagedMaterialParameterValue* outValue) {

		if (!outValue || parameterID == 0 ||
			!ResolveRenderFeaturePass(passID, generation)) {

			return 0;
		}
		const RenderFeaturePassRuntimeOverride* pass =
			RenderFeatureRuntimeOverrides::GetInstance().Find(UUID{ passID });
		const MaterialParameterValue* value = pass ?
			pass->parameters.Find(MaterialParameterID{ parameterID }) : nullptr;
		return value && EncodeMaterialParameterValue(*value, *outValue) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ClearRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID) {

		return ResolveRenderFeaturePass(passID, generation) &&
			parameterID != 0 &&
			RenderFeatureRuntimeOverrides::GetInstance().ClearParameter(
				UUID{ passID }, MaterialParameterID{ parameterID }) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ResetRenderFeaturePassCallback(
		uint64_t passID, uint64_t generation) {

		return ResolveRenderFeaturePass(passID, generation) &&
			RenderFeatureRuntimeOverrides::GetInstance().ResetPass(
				UUID{ passID }) ? 1 : 0;
	}

	void ManagedScriptRuntime::ResetRenderFeatureOverridesCallback() {

		RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	}
}
