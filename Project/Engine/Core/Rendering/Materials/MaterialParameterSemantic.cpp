#include "MaterialParameter.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cctype>

namespace {

	// 標準用途に対応する名前
	struct SemanticAlias {

		std::string_view name; // 正規化した名前
		Engine::MaterialParameterSemantic semantic = Engine::MaterialParameterSemantic::None; // 標準用途
	};

	// 既存の標準名と別名の対応
	constexpr std::array kSemanticAliases = {
		SemanticAlias{ "color", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "basecolor", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "albedo", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "maintexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "basecolortexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "albedotexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "normaltexture", Engine::MaterialParameterSemantic::NormalTexture },
		SemanticAlias{ "metallic", Engine::MaterialParameterSemantic::Metallic },
		SemanticAlias{ "metallicroughnesstexture", Engine::MaterialParameterSemantic::MetallicRoughnessTexture },
		SemanticAlias{ "metallictexture", Engine::MaterialParameterSemantic::MetallicTexture },
		SemanticAlias{ "roughness", Engine::MaterialParameterSemantic::Roughness },
		SemanticAlias{ "roughnesstexture", Engine::MaterialParameterSemantic::RoughnessTexture },
		SemanticAlias{ "displacementtexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "disptexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "heighttexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "displacementscale", Engine::MaterialParameterSemantic::DisplacementScale },
		SemanticAlias{ "displacementmidpoint", Engine::MaterialParameterSemantic::DisplacementMidpoint },
		SemanticAlias{ "ambientocclusion", Engine::MaterialParameterSemantic::AmbientOcclusion },
		SemanticAlias{ "ao", Engine::MaterialParameterSemantic::AmbientOcclusion },
		SemanticAlias{ "ambientocclusiontexture", Engine::MaterialParameterSemantic::AmbientOcclusionTexture },
		SemanticAlias{ "occlusiontexture", Engine::MaterialParameterSemantic::AmbientOcclusionTexture },
		SemanticAlias{ "aotexture", Engine::MaterialParameterSemantic::AmbientOcclusionTexture },
		SemanticAlias{ "emissivecolor", Engine::MaterialParameterSemantic::EmissiveColor },
		SemanticAlias{ "emissioncolor", Engine::MaterialParameterSemantic::EmissiveColor },
		SemanticAlias{ "emissivetexture", Engine::MaterialParameterSemantic::EmissiveTexture },
		SemanticAlias{ "emissiontexture", Engine::MaterialParameterSemantic::EmissiveTexture },
		SemanticAlias{ "emissiveintensity", Engine::MaterialParameterSemantic::EmissiveIntensity },
		SemanticAlias{ "opacity", Engine::MaterialParameterSemantic::Opacity },
		SemanticAlias{ "opacitytexture", Engine::MaterialParameterSemantic::OpacityTexture },
		SemanticAlias{ "alphaclip", Engine::MaterialParameterSemantic::AlphaClip },
		SemanticAlias{ "alphacutoff", Engine::MaterialParameterSemantic::AlphaClip },
		SemanticAlias{ "uvtransform", Engine::MaterialParameterSemantic::UVTransform },
	};

	// 区切りを除き小文字へ揃える
	std::string NormalizeParameterName(std::string_view name) {

		std::string normalized;
		normalized.reserve(name.size());
		for (const char character : name) {
			if (character == '_' || character == '-' || character == ' ') {
				continue;
			}
			normalized.push_back(static_cast<char>(
				std::tolower(static_cast<unsigned char>(character))));
		}
		return normalized;
	}

}

Engine::MaterialParameterSemantic Engine::ResolveMaterialParameterSemantic(std::string_view name) {

	// 名前を正規化して既存の別名表へ照合する
	const std::string normalized = NormalizeParameterName(name);
	for (const SemanticAlias& alias : kSemanticAliases) {
		if (alias.name == normalized) {
			return alias.semantic;
		}
	}
	return MaterialParameterSemantic::None;
}

bool Engine::IsSRGBMaterialTexture(MaterialParameterSemantic semantic) {

	return semantic == MaterialParameterSemantic::BaseColorTexture ||
		semantic == MaterialParameterSemantic::EmissiveTexture;
}
