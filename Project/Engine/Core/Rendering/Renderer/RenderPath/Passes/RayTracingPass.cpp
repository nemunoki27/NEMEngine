#include "RayTracingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingExecutor.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

//============================================================================
//	RayTracingPass classMethods
//============================================================================
void Engine::RayTracingPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context) {

	if (!deps_.assetLibrary || !deps_.raytracingPipelineCache ||
		!deps_.rayTracingExecutor || !context.sceneInstance ||
		!graphicsCore.GetDXObject().ShouldUseDispatchRays() ||
		!context.raytracing.tlasResource) {
		return;
	}

	AssetID profileID = context.sceneInstance->header.rayTracingProfile;
	if (!profileID) {
		profileID = BuiltinAssets::RayTracingProfiles::Default;
	}
	const RayTracingProfileAsset* profile =
		deps_.assetLibrary->LoadRayTracingProfile(profileID);
	if (!profile) {
		return;
	}

	for (const RayTracingEffectSettings& effect : profile->effects) {
		const RayTracingEffectRuntimeOverride* runtimeOverride =
			RayTracingRuntimeOverrides::GetInstance().Find(effect.name);
		const bool enabled = runtimeOverride &&
			runtimeOverride->enabled.has_value() ?
			*runtimeOverride->enabled : effect.enabled;
		if (!enabled || effect.executionPoint != executionPoint_) {
			continue;
		}
		if ((context.kind == RenderViewKind::Game && !effect.gameView) ||
			(context.kind == RenderViewKind::Scene && !effect.sceneView)) {
			continue;
		}
		deps_.rayTracingExecutor->Execute(graphicsCore, context,
			*deps_.assetLibrary, *deps_.raytracingPipelineCache,
			effect, runtimeOverride);
	}
}

Engine::RenderPathPassKind Engine::RayTracingPass::GetKind() const {

	return executionPoint_ == RayTracingExecutionPoint::AfterLighting ?
		RenderPathPassKind::RayTracingAfterLighting :
		RenderPathPassKind::RayTracingAfterTransparent;
}
