#include "ShaderGraphAssetAuthoring.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphPublication.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>

using namespace Engine;
using namespace Engine::ShaderGraphPublication;

AssetID Engine::ShaderGraphAssetAuthoring::Create(
	AssetDatabase& database, const ShaderGraphCreationRequest& request, std::string& status) {

	if (request.path.empty()) {
		status = "作成先を設定してください";
		return {};
	}
	// 保存先と種類を検証してGraphを作成する
	const std::filesystem::path path = database.ResolveAssetPath(request.path);
	if (path.empty()) {
		status = "GameAssets内を指定してください";
		return {};
	}
	if (std::filesystem::exists(path)) {
		status = "同名のグラフが存在します";
		return {};
	}

	const std::string name = GraphFileStem(path);
	ShaderGraphAsset graph{};
	if (request.domain == ShaderGraphDomain::PostProcess) {
		graph = CreateDefaultPostProcessShaderGraph(name);
	} else if (request.domain == ShaderGraphDomain::RayTracingEffect) {
		graph = CreateDefaultRayTracingEffectShaderGraph(name);
	} else {
		graph = CreateDefaultSurfaceShaderGraph(name, request.target);
	}
	// 文書・meta・索引を一括で作成する
	const AssetID assetID = SaveGraph(database, graph, path, {}, status);
	if (!assetID) {
		return {};
	}
	status = "グラフを作成しました";
	return assetID;
}

bool Engine::ShaderGraphAssetAuthoring::Import(AssetDatabase& database, const ShaderGraphAsset& destination,
	AssetID destinationID, AssetID source, ShaderGraphAsset& output, std::string& status) {

	const AssetMeta* meta = database.Find(source);
	if (!meta || source == destinationID) {
		status = "別のMaterialまたはShaderGraphを指定してください";
		return false;
	}
	// 共通の型互換判定で参照先を読み込む
	const auto resolver = [&database](AssetID id, AssetType type, nlohmann::json& data) {
		const AssetMeta* dependency = database.Find(id);
		if (!dependency || !IsAssetTypeCompatible(type, dependency->type)) {
			return false;
		}
		const auto path = database.ResolveFullPath(id);
		if (path.empty() || !std::filesystem::exists(path)) {
			return false;
		}
		if (type == AssetType::Texture || type == AssetType::Unknown) {
			data = {{"path", Algorithm::PathToUTF8(path)}};
			return true;
		}
		data = JsonAdapter::Load(path, false);
		return !data.is_null();
	};
	if (!ShaderGraphSettingsImporter::Import(destination, source, meta->type, resolver, output, status)) {
		return false;
	}
	return true;
}
