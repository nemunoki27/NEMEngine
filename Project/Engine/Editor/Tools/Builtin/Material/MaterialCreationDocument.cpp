#include "MaterialCreationDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

namespace Engine::MaterialCreationDocument {

	// D3D12列挙を保存用の文字列へ変換する
	template <typename T>
	std::string EnumToJsonString(T value) {

		return std::string(Engine::EnumAdapter<T>::ToString(value));
	}

	// 作成タイプをマテリアル用途へ変換する
	Engine::MaterialUsage ToMaterialUsage(Engine::MaterialCreateType type) {

		switch (type) {
		case Engine::MaterialCreateType::Mesh:
			return Engine::MaterialUsage::Mesh;
		case Engine::MaterialCreateType::Particle:
			return Engine::MaterialUsage::Particle;
		case Engine::MaterialCreateType::Sprite:
			return Engine::MaterialUsage::Sprite;
		case Engine::MaterialCreateType::Text:
			return Engine::MaterialUsage::Text;
		case Engine::MaterialCreateType::Line:
			return Engine::MaterialUsage::Line;
		default:
			return Engine::MaterialUsage::Generic;
		}
	}

	// HLSLをGUIDで参照するステージを作る
	static nlohmann::json MakeStageJson(const char* stage, Engine::AssetID hlsl, const char* entry, const char* profile) {

		return nlohmann::json{
			{"stage", stage},
			{"file", Engine::ToAssetReferenceJson(hlsl)},
			{"entry", entry},
			{"profile", profile},
		};
	}

	// 描画タイプに合わせてShaderのステージを作る
	nlohmann::json MakeShaderJson(const std::string& name, Engine::AssetID vs, Engine::AssetID ps, Engine::AssetID ms,
		Engine::AssetID as, Engine::AssetID gs, bool includeMeshStages, bool includeGeometryStage, bool pixelOnly,
		const std::string& pixelEntry) {

		nlohmann::json stages = nlohmann::json::array();
		// VSとMSの描画経路を登録する
		if (!pixelOnly) {
			stages.push_back(MakeStageJson("VS", vs, "main", "vs_6_0"));
		}
		if (includeMeshStages && as) {
			stages.push_back(MakeStageJson("AS", as, "main", "as_6_6"));
		}
		if (includeMeshStages && ms) {
			stages.push_back(MakeStageJson("MS", ms, "main", "ms_6_6"));
		}
		if (includeGeometryStage && gs) {
			stages.push_back(MakeStageJson("GS", gs, "main", "gs_6_0"));
		}
		// 指定されたPixel入口を使う
		stages.push_back(MakeStageJson("PS", ps, pixelEntry.c_str(), "ps_6_0"));

		return nlohmann::json{{"name", name + "Shader"}, {"stages", stages}};
	}

	// 1バリアント分のpipeline.jsonを作る
	static nlohmann::json MakePipelineVariantJson(Engine::AssetID shaderID, const char* kind, const char* pipelineType,
		bool useGeometryShader, bool requiresMeshShader, int numRenderTargets, const Engine::PipelineCreateSettings& settings) {

		const char* topology =
			useGeometryShader ? "D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE" : "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE";

		nlohmann::json variant{
			{"kind", kind},
			{"pipelineType", pipelineType},
			{"shader", Engine::ToAssetReferenceJson(shaderID)},
			{"numRenderTargets", numRenderTargets},
			{"dynamicRenderTargetFormats", true},
			{"dsvFormat", "DXGI_FORMAT_UNKNOWN"},
			{"topologyType", topology},
			{"rasterizer",
				{
					{"fillMode", EnumToJsonString(settings.fillMode)},
					{"cullMode", EnumToJsonString(settings.cullMode)},
					{"frontCounterClockwise", settings.frontCounterClockwise},
					{"depthClipEnable", settings.depthClipEnable},
				}},
			{"depthStencil",
				{
					{"depthEnable", settings.depthEnable},
					{"depthWriteMask", EnumToJsonString(settings.depthWriteMask)},
					{"depthFunc", EnumToJsonString(settings.depthFunc)},
					{"stencilEnable", settings.stencilEnable},
				}},
			{"staticSamplers", nlohmann::json::array({{
								   {"shaderRegister", 0},
								   {"registerSpace", 0},
								   {"filter", EnumToJsonString(settings.samplerFilter)},
								   {"addressU", EnumToJsonString(settings.samplerAddressU)},
								   {"addressV", EnumToJsonString(settings.samplerAddressV)},
								   {"addressW", EnumToJsonString(settings.samplerAddressW)},
								   {"comparisonFunc", EnumToJsonString(settings.samplerComparison)},
								   {"borderColor", EnumToJsonString(settings.samplerBorderColor)},
								   {"maxAnisotropy", settings.samplerMaxAnisotropy},
								   {"mipLODBias", settings.samplerMipLODBias},
								   {"minLOD", settings.samplerMinLOD},
								   {"maxLOD", settings.samplerMaxLOD},
								   {"shaderVisibility", "D3D12_SHADER_VISIBILITY_PIXEL"},
							   }})},
		};
		if (requiresMeshShader) {
			variant["requiresMeshShader"] = true;
		}
		return variant;
	}

	// VSとMSの描画経路を持つPipelineを作る
	nlohmann::json MakePipelineJson(const std::string& name, Engine::AssetID shaderID, bool useMeshShader,
		bool useGeometryShader, int numRenderTargets, const Engine::PipelineCreateSettings& settings) {

		nlohmann::json variants = nlohmann::json::array();
		if (useMeshShader) {

			variants.push_back(
				MakePipelineVariantJson(shaderID, "GraphicsMesh", "Mesh", false, true, numRenderTargets, settings));
			variants.push_back(
				MakePipelineVariantJson(shaderID, "GraphicsVertex", "Vertex", false, false, numRenderTargets, settings));
		} else {

			variants.push_back(MakePipelineVariantJson(shaderID, useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex",
				useGeometryShader ? "Geometry" : "Vertex", useGeometryShader, false, numRenderTargets, settings));
		}
		return nlohmann::json{{"name", name + "Pipeline"}, {"variants", std::move(variants)}};
	}

	// 描画タイプに合わせてMaterialのパスを作る
	nlohmann::json MakeMaterialJson(const std::string& name, Engine::AssetID pipelineID, Engine::AssetID transparentPipelineID,
		Engine::AssetID shaderOverride, Engine::MaterialCreateType type, bool useMeshShader, bool useGeometryShader,
		const Engine::PipelineCreateSettings& settings) {

		// 描画領域と使用variantを選ぶ
		const bool surfaceDomain = (type == Engine::MaterialCreateType::Mesh) ||
								   (type == Engine::MaterialCreateType::Particle) || (type == Engine::MaterialCreateType::Line);
		const char* domain = surfaceDomain ? "Surface" : "UI";
		const char* preferredVariant =
			useMeshShader ? "GraphicsMesh" : (useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex");
		auto makePass = [&](const char* passKind, Engine::AssetID passPipelineID) {
			nlohmann::json pass{
				{"passKind", passKind},
				{"pipeline", Engine::ToAssetReferenceJson(passPipelineID)},
				{"preferredVariant", preferredVariant},
			};
			if (shaderOverride) {
				pass["shaderOverride"] = Engine::ToAssetReferenceJson(shaderOverride);
			}
			return pass;
		};

		nlohmann::json passes = nlohmann::json::array();
		// Meshだけ通常描画と半透明描画を分ける
		if (type == Engine::MaterialCreateType::Mesh) {
			passes.push_back(makePass("Draw", pipelineID));
			if (transparentPipelineID) {
				passes.push_back(makePass("Transparent", transparentPipelineID));
			}
		} else {
			passes.push_back(makePass(type == Engine::MaterialCreateType::Particle ? "Transparent" : "Draw", pipelineID));
		}

		return nlohmann::json{
			{"name", name},
			{"domain", domain},
			{"usage", Engine::EnumAdapter<Engine::MaterialUsage>::ToString(ToMaterialUsage(type))},
			{"renderState",
				{
					{"overridesRenderer", true},
					{"surfaceMode", Engine::EnumAdapter<Engine::MaterialSurfaceMode>::ToString(settings.surfaceMode)},
					{"phase", std::string(Engine::ToString(Engine::ResolveMaterialRenderPhase(settings.surfaceMode)))},
					{"blendMode", Engine::EnumAdapter<Engine::BlendMode>::ToString(settings.blendMode)},
				}},
			{"passes", std::move(passes)},
			{"parameters", nlohmann::json::object()},
		};
	}
}
