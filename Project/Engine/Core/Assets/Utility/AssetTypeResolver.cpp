#include "AssetTypeResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

//============================================================================
//	AssetTypeResolver classMethods
//============================================================================
Engine::AssetType Engine::AssetTypeResolver::GuessByPath(const std::filesystem::path& assetFullPath) {

	// 比較対象はすべて小文字へ寄せる。複合サフィックスはfilename、単一拡張子はextensionで見る
	const std::string filename = Algorithm::ToLower(assetFullPath.filename().string());
	const std::string extension = Algorithm::ToLower(assetFullPath.extension().string());

	// 複合サフィックスを先に判定する(.scene.jsonなどは拡張子だけでは区別できないため)
	if (Algorithm::EndsWith(filename, ".scene.json") || extension == ".scene") {
		return AssetType::Scene;
	}
	if (Algorithm::EndsWith(filename, ".collisionsettings.json")) {
		return AssetType::CollisionSettings;
	}
	if (Algorithm::EndsWith(filename, ".postprocessstack.json")) {
		return AssetType::PostProcessStack;
	}
	if (Algorithm::EndsWith(filename, ".prefab.json") || extension == ".prefab") {
		return AssetType::Prefab;
	}
	if (Algorithm::EndsWith(filename, ".material.json") || extension == ".material") {
		return AssetType::Material;
	}
	if (Algorithm::EndsWith(filename, ".shader.json") || extension == ".shader" ||
		extension == ".hlsl" || extension == ".hlsli") {
		return AssetType::Shader;
	}
	if (Algorithm::EndsWith(filename, ".pipeline.json")) {
		return AssetType::RenderPipeline;
	}
	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
		extension == ".dds" || extension == ".tga" || extension == ".bmp") {
		return AssetType::Texture;
	}
	if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
		return AssetType::Mesh;
	}
	if (Algorithm::EndsWith(filename, ".font.json") || Algorithm::EndsWith(filename, ".msdf.json") ||
		extension == ".ttf" || extension == ".otf" || extension == ".fnt" || extension == ".font") {
		return AssetType::Font;
	}
	if (extension == ".cs") {
		return AssetType::Script;
	}
	if (extension == ".wav" || extension == ".wave" || extension == ".mp3") {
		return AssetType::Audio;
	}
	if (Algorithm::EndsWith(filename, ".animclip.json") || extension == ".animclip") {
		return AssetType::AnimationClip;
	}
	return AssetType::Unknown;
}

bool Engine::AssetTypeResolver::IsJsonAssetType(AssetType type) {

	// 内部にguid/参照を持つ可能性があるJSONベースのアセット種別
	switch (type) {
	case AssetType::Scene:
	case AssetType::Prefab:
	case AssetType::Material:
	case AssetType::AnimationClip:
	case AssetType::Shader:
	case AssetType::RenderPipeline:
	case AssetType::PostProcessStack:
	case AssetType::CollisionSettings:
		return true;
	default:
		return false;
	}
}
