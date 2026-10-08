#include "AssetTypeResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>

//============================================================================
//	Internal Asset Suffix Helpers
//============================================================================
namespace {

	struct CompoundAssetSuffix {

		std::string_view suffix;
		Engine::AssetType type = Engine::AssetType::DefaultAsset;
	};

	constexpr std::array<CompoundAssetSuffix, 17> kCompoundAssetSuffixes = {
		CompoundAssetSuffix{ ".animcontroller.json", Engine::AssetType::AnimationController },
		CompoundAssetSuffix{ ".scene.json", Engine::AssetType::Scene },
		CompoundAssetSuffix{ ".rendertexture.json", Engine::AssetType::RenderTexture },
		CompoundAssetSuffix{ ".renderpasses.json", Engine::AssetType::RenderPasses },
		CompoundAssetSuffix{ ".effect.json", Engine::AssetType::ParticleEffect },
		CompoundAssetSuffix{ ".prefab.json", Engine::AssetType::Prefab },
		CompoundAssetSuffix{ ".material.json", Engine::AssetType::Material },
		CompoundAssetSuffix{ ".shadergraph.json", Engine::AssetType::ShaderGraph },
		CompoundAssetSuffix{ ".graph.json", Engine::AssetType::ShaderGraph },
		CompoundAssetSuffix{ ".shader.json", Engine::AssetType::Shader },
		CompoundAssetSuffix{ ".pipeline.json", Engine::AssetType::RenderPipeline },
		CompoundAssetSuffix{ ".font.json", Engine::AssetType::Font },
		CompoundAssetSuffix{ ".msdf.json", Engine::AssetType::Font },
		CompoundAssetSuffix{ ".animclip.json", Engine::AssetType::AnimationClip },
		CompoundAssetSuffix{ ".execonfig.json", Engine::AssetType::DefaultAsset },
		CompoundAssetSuffix{ ".materialsettings.json", Engine::AssetType::DefaultAsset },
		CompoundAssetSuffix{ ".actor.json", Engine::AssetType::DefaultAsset },
	};

	const CompoundAssetSuffix* FindCompoundAssetSuffix(
		const std::filesystem::path& assetFullPath) {

		const std::string fileName = Engine::Algorithm::ToLower(
			Engine::Algorithm::PathToUTF8(assetFullPath.filename()));
		for (const CompoundAssetSuffix& entry : kCompoundAssetSuffixes) {
			if (Engine::Algorithm::EndsWith(fileName,
				std::string(entry.suffix))) {
				return &entry;
			}
		}
		return nullptr;
	}
}

//============================================================================
//	AssetTypeResolver classMethods
//============================================================================
Engine::AssetType Engine::AssetTypeResolver::GuessByPath(const std::filesystem::path& assetFullPath) {

	// 比較対象はすべて小文字へ寄せて複合サフィックスはfilename、単一拡張子はextensionで見る
	const std::string extension = Algorithm::ToLower(
		Algorithm::PathToUTF8(assetFullPath.extension()));

	// 複合サフィックスを先に判定する(.scene.jsonなどは拡張子だけでは区別できないため)
	if (const CompoundAssetSuffix* compound =
		FindCompoundAssetSuffix(assetFullPath)) {

		return compound->type;
	}
	if (extension == ".scene") { return AssetType::Scene; }
	if (extension == ".effect") { return AssetType::ParticleEffect; }
	if (extension == ".prefab") { return AssetType::Prefab; }
	if (extension == ".material") { return AssetType::Material; }
	if (extension == ".shadergraph") { return AssetType::ShaderGraph; }
	if (extension == ".shader" || extension == ".hlsl" ||
		extension == ".hlsli") {
		return AssetType::Shader;
	}
	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
		extension == ".dds" || extension == ".tga" || extension == ".bmp" ||
		extension == ".gif" || extension == ".hdr") {
		return AssetType::Texture;
	}
	if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
		return AssetType::Mesh;
	}
	if (extension == ".ttf" || extension == ".otf" ||
		extension == ".fnt" || extension == ".font") {
		return AssetType::Font;
	}
	if (extension == ".cs") {
		return AssetType::Script;
	}
	if (extension == ".wav" || extension == ".wave" || extension == ".mp3") {
		return AssetType::Audio;
	}
	if (extension == ".animclip") {
		return AssetType::AnimationClip;
	}
	return AssetType::DefaultAsset;
}

std::string_view Engine::AssetTypeResolver::FindCompoundSuffix(
	const std::filesystem::path& assetFullPath) {

	const CompoundAssetSuffix* suffix =
		FindCompoundAssetSuffix(assetFullPath);
	return suffix ? suffix->suffix : std::string_view{};
}

bool Engine::AssetTypeResolver::IsJsonAssetType(AssetType type) {

	// 内部にguid/参照を持つ可能性があるJSONベースのアセット種別
	switch (type) {
	case AssetType::Scene:
	case AssetType::Prefab:
	case AssetType::Material:
	case AssetType::AnimationClip:
	case AssetType::AnimationController:
	case AssetType::Shader:
	case AssetType::RenderPipeline:
	case AssetType::ParticleEffect:
	case AssetType::ShaderGraph:
	case AssetType::RenderTexture:
	case AssetType::RenderPasses:
		return true;
	default:
		return false;
	}
}

bool Engine::AssetTypeResolver::IsJsonAssetFile(AssetType type, const std::filesystem::path& path) {

	const auto extension = Algorithm::ToLower(Algorithm::PathToUTF8(path.extension()));
	if (type == AssetType::Font) {
		return extension == ".json";
	}
	if (!IsJsonAssetType(type)) {
		return false;
	}
	return extension == ".json" || extension == ".scene" || extension == ".prefab" ||
		extension == ".effect" || extension == ".material" || extension == ".shadergraph" ||
		extension == ".shader" || extension == ".animclip";
}
