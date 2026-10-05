#include "MaterialCreationImportUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <utility>

namespace Engine::MaterialCreationImportUtility {

	// 保存文字列をD3D12列挙へ戻す
	template <typename T>
	T EnumFromJsonString(const nlohmann::json& object, const char* key, T fallback) {

		if (!object.contains(key) || !object[key].is_string()) {
			return fallback;
		}
		return Engine::EnumAdapter<T>::FromString(object[key].get<std::string>()).value_or(fallback);
	}

	// マテリアル用途を作成タイプへ変換する
	std::optional<Engine::MaterialCreateType> ToMaterialCreateType(Engine::MaterialUsage usage) {

		switch (usage) {
		case Engine::MaterialUsage::Mesh:
			return Engine::MaterialCreateType::Mesh;
		case Engine::MaterialUsage::Particle:
			return Engine::MaterialCreateType::Particle;
		case Engine::MaterialUsage::Sprite:
			return Engine::MaterialCreateType::Sprite;
		case Engine::MaterialUsage::Text:
			return Engine::MaterialCreateType::Text;
		case Engine::MaterialUsage::Line:
			return Engine::MaterialCreateType::Line;
		default:
			return std::nullopt;
		}
	}

	// Pipelineの先頭variantから設定を読む
	bool ReadPipelineSettings(
		Engine::AssetDatabase& assetDatabase, Engine::AssetID pipelineID, Engine::PipelineCreateSettings& outSettings) {

		// 取込完了まで呼出し元の設定を保持する
		try {
			PipelineCreateSettings nextSettings = outSettings;

			const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
			const nlohmann::json pipelineData =
				pipelinePath.empty() ? nlohmann::json{} : Engine::JsonAdapter::Load(pipelinePath.string(), false);
			if (!pipelineData.is_object() || !pipelineData.contains("variants") || !pipelineData["variants"].is_array() ||
				pipelineData["variants"].empty()) {
				return false;
			}

			const nlohmann::json& variant = pipelineData["variants"].front();
			if (!variant.is_object()) {
				return false;
			}
			if (variant.contains("rasterizer") && variant["rasterizer"].is_object()) {
				const auto& rasterizer = variant["rasterizer"];
				nextSettings.fillMode = EnumFromJsonString(rasterizer, "fillMode", nextSettings.fillMode);
				nextSettings.cullMode = EnumFromJsonString(rasterizer, "cullMode", nextSettings.cullMode);
				nextSettings.frontCounterClockwise =
					rasterizer.value("frontCounterClockwise", nextSettings.frontCounterClockwise);
				nextSettings.depthClipEnable = rasterizer.value("depthClipEnable", nextSettings.depthClipEnable);
			}
			if (variant.contains("depthStencil") && variant["depthStencil"].is_object()) {
				const auto& depthStencil = variant["depthStencil"];
				nextSettings.depthEnable = depthStencil.value("depthEnable", nextSettings.depthEnable);
				nextSettings.depthWriteMask = EnumFromJsonString(depthStencil, "depthWriteMask", nextSettings.depthWriteMask);
				nextSettings.depthFunc = EnumFromJsonString(depthStencil, "depthFunc", nextSettings.depthFunc);
				nextSettings.stencilEnable = depthStencil.value("stencilEnable", nextSettings.stencilEnable);
			}
			if (variant.contains("staticSamplers") && variant["staticSamplers"].is_array() &&
				!variant["staticSamplers"].empty()) {
				const auto& sampler = variant["staticSamplers"].front();
				if (!sampler.is_object()) {
					return false;
				}
				nextSettings.samplerFilter = EnumFromJsonString(sampler, "filter", nextSettings.samplerFilter);
				nextSettings.samplerAddressU = EnumFromJsonString(sampler, "addressU", nextSettings.samplerAddressU);
				nextSettings.samplerAddressV = EnumFromJsonString(sampler, "addressV", nextSettings.samplerAddressV);
				nextSettings.samplerAddressW = EnumFromJsonString(sampler, "addressW", nextSettings.samplerAddressW);
				nextSettings.samplerComparison = EnumFromJsonString(sampler, "comparisonFunc", nextSettings.samplerComparison);
				nextSettings.samplerBorderColor = EnumFromJsonString(sampler, "borderColor", nextSettings.samplerBorderColor);
				nextSettings.samplerMaxAnisotropy = sampler.value("maxAnisotropy", nextSettings.samplerMaxAnisotropy);
				nextSettings.samplerMipLODBias = sampler.value("mipLODBias", nextSettings.samplerMipLODBias);
				nextSettings.samplerMinLOD = sampler.value("minLOD", nextSettings.samplerMinLOD);
				nextSettings.samplerMaxLOD = sampler.value("maxLOD", nextSettings.samplerMaxLOD);
			}
			outSettings = nextSettings;
			return true;

		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}

	// MaterialのパスからShaderのステージを読む
	bool ReadShaderReferences(
		Engine::AssetDatabase& assetDatabase, const nlohmann::json& pass, MaterialShaderReferences& outReferences) {

		// 不正なステージでは既存の参照を変更しない
		try {
			MaterialShaderReferences nextReferences;
			if (!pass.is_object()) {
				return false;
			}

			Engine::AssetID shaderID = Engine::ParseAssetID(pass, "shaderOverride");
			if (!shaderID) {
				const Engine::AssetID pipelineID = Engine::ParseAssetID(pass, "pipeline");
				const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
				const nlohmann::json pipelineData =
					pipelinePath.empty() ? nlohmann::json{} : Engine::JsonAdapter::Load(pipelinePath.string(), false);
				if (!pipelineData.is_object() || !pipelineData.contains("variants") || !pipelineData["variants"].is_array() ||
					pipelineData["variants"].empty()) {
					return false;
				}
				shaderID = Engine::ParseAssetID(pipelineData["variants"].front(), "shader");
			}
			const std::filesystem::path shaderPath = assetDatabase.ResolveFullPath(shaderID);
			const nlohmann::json shaderData =
				shaderPath.empty() ? nlohmann::json{} : Engine::JsonAdapter::Load(shaderPath.string(), false);
			if (!shaderData.is_object() || !shaderData.contains("stages") || !shaderData["stages"].is_array()) {
				return false;
			}

			for (const auto& stageData : shaderData["stages"]) {
				const std::string stage = stageData.value("stage", std::string{});
				const Engine::AssetID hlsl = Engine::ParseAssetID(stageData, "file");
				if (stage == "VS") {
					nextReferences.vs = hlsl;
				} else if (stage == "PS") {
					nextReferences.ps = hlsl;
					nextReferences.psEntry = stageData.value("entry", std::string("main"));
				} else if (stage == "MS") {
					nextReferences.ms = hlsl;
				} else if (stage == "AS") {
					nextReferences.as = hlsl;
				} else if (stage == "GS") {
					nextReferences.gs = hlsl;
				}
			}
			if (!nextReferences.ps) {
				return false;
			}
			outReferences = std::move(nextReferences);
			return true;

		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}
}
