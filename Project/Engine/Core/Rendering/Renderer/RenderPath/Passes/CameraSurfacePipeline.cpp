#include "CameraSurfacePipeline.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>

//============================================================================
//	CameraSurfacePipeline functions
//============================================================================
Engine::RenderFeatureProfileAsset Engine::BuildCameraReflectionProfile(const CameraCommon& settings) {

	RenderFeatureProfileAsset profile{};
	{
		RenderFeaturePassSettings pass{};
		pass.id = UUID{0x52545245464c4543ull};
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
		pass.source = {UUID{0x52545245464c4543ull}, "Raw"};
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
		pass.passInputs["gReflectionHitDistance"] = {UUID{0x52545245464c4543ull}, "HitDistance"};
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
		pass.passInputs["gReflectionHitDistance"] = {UUID{0x52545245464c4543ull}, "HitDistance"};
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
		pass.passInputs["gReflectionHitDistance"] = {UUID{0x52545245464c4543ull}, "HitDistance"};
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
		pass.passInputs["gReflectionHitDistance"] = {UUID{0x52545245464c4543ull}, "HitDistance"};
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
		pass.passInputs["gCurrentHitGeometry"] = {UUID{0x52545245464c4543ull}, "HitGeometry"};
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
	// 移行済みの調整値だけを固定パスへ戻す
	nlohmann::json savedPasses = nlohmann::json::array();
	if (settings.reflectionParameters.is_object() && settings.reflectionParameters.contains("passes") &&
		settings.reflectionParameters["passes"].is_array()) {
		savedPasses = settings.reflectionParameters["passes"];
	}
	const auto saved = RenderFeatureProfileSerializer::FromJson(nlohmann::json{{"passes", savedPasses}});
	for (const auto& oldPass : saved.passes) {
		for (auto& pass : profile.passes) {
			if (pass.id == oldPass.id) pass.parameterOverrides.MergeFrom(oldPass.parameterOverrides);
		}
	}
	return profile;
}
