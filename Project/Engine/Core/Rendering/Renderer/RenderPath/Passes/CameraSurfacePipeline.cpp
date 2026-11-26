#include "CameraSurfacePipeline.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

//============================================================================
//	CameraSurfacePipeline functions
//============================================================================
Engine::RenderFeatureProfileAsset Engine::BuildCameraReflectionProfile(const SurfaceLightingSettings& settings) {

	RenderFeatureProfileAsset profile{};
	{
		RenderFeaturePassSettings pass{};
		pass.id = CameraSurfacePassIDs::ReflectionTrace;
		pass.name = "Reflection Trace";
		pass.type = RenderFeaturePassType::RayTracing;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7cull};
		pass.materialPass = MaterialPassKind::RayTracing;
		pass.sourceKind = RenderFeatureSourceKind::SceneColor;
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Raw", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "HitDistance", .shaderResource = "gReflectionHitDistance",
			.format = RenderFeatureTextureFormat::R16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "HitGeometry", .shaderResource = "gReflectionHitGeometry",
			.format = RenderFeatureTextureFormat::RGBA32_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceAlbedo"] = "SceneColorMain";
		pass.sceneInputs["gSourceEmissive"] = "SceneEmissiveMain";
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceFlags"] = "SceneFlagsMain";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x5254535041544941ull};
		pass.name = "Reflection Anti-firefly";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7dull};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::PassOutput;
		pass.source = {CameraSurfacePassIDs::ReflectionTrace, "Raw"};
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Filtered", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gReflectionHitDistance"] = {CameraSurfacePassIDs::ReflectionTrace, "HitDistance"};
		pass.parameterOverrides.Set("antiFirefly", MaterialParameterValue{.value = 1.0f});
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x5254535041544931ull};
		pass.name = "Reflection Spatial";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7dull};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::PassOutput;
		pass.source = {UUID{0x5254535041544941ull}, "Filtered"};
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Spatial", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gReflectionHitDistance"] = {CameraSurfacePassIDs::ReflectionTrace, "HitDistance"};
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x5254535041544932ull};
		pass.name = "Reflection Spatial 2";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7dull};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::PassOutput;
		pass.source = {UUID{0x5254535041544931ull}, "Spatial"};
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Spatial", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gReflectionHitDistance"] = {CameraSurfacePassIDs::ReflectionTrace, "HitDistance"};
		pass.parameterOverrides.Set("stepWidth", MaterialParameterValue{.value = 2.0f});
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x5254535041544933ull};
		pass.name = "Reflection Spatial 3";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7dull};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::PassOutput;
		pass.source = {UUID{0x5254535041544932ull}, "Spatial"};
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Spatial", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gReflectionHitDistance"] = {CameraSurfacePassIDs::ReflectionTrace, "HitDistance"};
		pass.parameterOverrides.Set("stepWidth", MaterialParameterValue{.value = 2.0f});
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x525454454d504f52ull};
		pass.name = "Reflection Temporal";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7eull};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::PassOutput;
		pass.source = {UUID{0x5254535041544933ull}, "Spatial"};
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Temporal", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = true, .historyShaderResource = "gHistoryColor",
		});
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Geometry", .shaderResource = "gHistoryGeometryOutput",
			.format = RenderFeatureTextureFormat::RGBA32_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = true, .historyShaderResource = "gHistoryGeometry",
		});
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Normal", .shaderResource = "gHistoryNormalOutput",
			.format = RenderFeatureTextureFormat::RG16_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = true, .historyShaderResource = "gHistoryNormal",
		});
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "HitGeometry", .shaderResource = "gHistoryHitGeometryOutput",
			.format = RenderFeatureTextureFormat::RGBA32_FLOAT,
			.widthScale = 0.5f, .heightScale = 0.5f,
			.history = true, .historyShaderResource = "gHistoryHitGeometry",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceMotion"] = "SceneMotionMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gCurrentHitGeometry"] = {CameraSurfacePassIDs::ReflectionTrace, "HitGeometry"};
		profile.passes.emplace_back(std::move(pass));
	}
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x5254434f4d504f53ull};
		pass.name = "Reflection Composite";
		pass.type = RenderFeaturePassType::Compute;
		pass.anchor = RenderFeatureAnchor::AfterLighting;
		pass.material = AssetID{0x4e454d4153534554ull, 0x375384bc3bf6bb7full};
		pass.materialPass = MaterialPassKind::PostProcess;
		pass.sourceKind = RenderFeatureSourceKind::SceneColor;
		pass.outputs.push_back(RenderFeatureOutputSettings{
			.name = "Color", .shaderResource = "gDestColor",
			.format = RenderFeatureTextureFormat::Inherit,
			.widthScale = 1.0f, .heightScale = 1.0f,
			.history = false, .historyShaderResource = "",
		});
		pass.sceneInputs["gSourceDepth"] = "SceneDepth";
		pass.sceneInputs["gSourceFlags"] = "SceneFlagsMain";
		pass.sceneInputs["gSourceMaterial"] = "SceneMaterialMain";
		pass.sceneInputs["gSourceNormal"] = "SceneNormalMain";
		pass.sceneInputs["gSourcePosition"] = "ScenePositionMain";
		pass.passInputs["gReflectionColor"] = {UUID{0x525454454d504f52ull}, "Temporal"};
		profile.passes.emplace_back(std::move(pass));
	}
	// Cameraの品質に合わせて中間解像度を選ぶ
	const float scale = settings.reflectionQuality == 0 ? 0.25f : settings.reflectionQuality == 2 ? 1.0f : 0.5f;
	for (auto& pass : profile.passes) {
		if (pass.id == UUID{0x5254434f4d504f53ull}) continue;
		for (auto& output : pass.outputs) output.widthScale = output.heightScale = scale;
	}
	// 公開している固定パスのパラメータだけを上書きする
	const auto saved = settings.reflectionParameters.find("passes");
	if (saved != settings.reflectionParameters.end() && saved->is_array()) {
		for (const auto& entry : *saved) {
			if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("parameters")) continue;
			const auto id = FromString16Hex(entry["id"].get<std::string>());
			for (auto& pass : profile.passes) {
				if (pass.id != id) continue;
				MaterialInstanceParameters overrides{};
				ReadMaterialInstance(entry["parameters"], overrides);
				pass.parameterOverrides.MergeFrom(overrides);
				break;
			}
		}
	}
	return profile;
}
