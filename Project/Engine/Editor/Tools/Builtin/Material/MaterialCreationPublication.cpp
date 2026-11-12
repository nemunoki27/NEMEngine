#include "MaterialCreationSession.h"
#include "MaterialCreationDocument.h"
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetPath.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//============================================================================
//	MaterialCreationSession classMethods
//============================================================================
using namespace Engine::MaterialCreationDocument;

bool Engine::MaterialCreationSession::CreateMaterialAssets(const EditorToolContext& context) {

	draft_.createMessage.clear();

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase) {
		draft_.createMessage = "AssetDatabaseが利用できません";
		return false;
	}
	if ((draft_.createType != MaterialCreateType::Particle && !draft_.createVS) || !draft_.createPS) {
		draft_.createMessage =
			draft_.createType == MaterialCreateType::Particle ? "PixelShaderは必須です" : "VertexShaderとPixelShaderは必須です";
		return false;
	}
	if (draft_.createPSEntry.empty()) {
		draft_.createMessage = "PSエントリーは必須です";
		return false;
	}
	const bool createMeshTransparent = draft_.createType == MaterialCreateType::Mesh && draft_.createTransparentPass;
	if (createMeshTransparent && (!draft_.createTransparentPS || draft_.createTransparentPSEntry.empty())) {
		draft_.createMessage = "半透明PixelShaderとPSエントリーは必須です";
		return false;
	}
	// Lineの太線展開にはGSが必要
	if (draft_.createType == MaterialCreateType::Line && !draft_.createGS) {
		draft_.createMessage = "LineはGeometryShaderも必須です";
		return false;
	}

	const auto plan = ProjectAssetFileUtility::PlanToolAsset(ProjectAssetFileKind::Material, draft_.createRelativePath);
	if (!plan.success) {
		draft_.createMessage = plan.message;
		return false;
	}
	const std::string baseName = ProjectAssetPath::SplitAssetFileName(plan.fullPath).first;
	const std::string basePath = plan.assetPath.substr(0, plan.assetPath.size() - std::string_view(".material.json").size());

	const bool useMeshShader = (draft_.createType == MaterialCreateType::Mesh) && static_cast<bool>(draft_.createMS);
	const bool useGeometryShader = (draft_.createType == MaterialCreateType::Line);
	const bool isParticle = draft_.createType == MaterialCreateType::Particle;
	const int numRenderTargets = draft_.createType == MaterialCreateType::Mesh ? 3 : 1;

	// ShaderとPipelineとMaterialの保存先を揃える
	const std::string shaderLogical = basePath + ".shader.json";
	const std::string pipelineLogical = basePath + ".pipeline.json";
	const std::string transparentShaderLogical = basePath + "Transparent.shader.json";
	const std::string transparentPipelineLogical = basePath + "Transparent.pipeline.json";
	const std::string materialLogical = plan.assetPath;

	// 保存前に取込元の用途とParameterを確認する
	nlohmann::json sourceParameters;
	if (draft_.createSourceMaterial) {
		try {
			const std::filesystem::path sourcePath = assetDatabase->ResolveFullPath(draft_.createSourceMaterial);
			const nlohmann::json sourceData =
				sourcePath.empty() ? nlohmann::json{} : JsonAdapter::Load(sourcePath.string(), false);
			if (!sourceData.is_object()) {
				draft_.createMessage = "取り込み元のMaterialを読み込めません";
				return false;
			}
			const MaterialUsage sourceUsage =
				EnumAdapter<MaterialUsage>::FromString(sourceData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
			if (sourceUsage == ToMaterialUsage(draft_.createType) && sourceData.contains("parameters") &&
				sourceData["parameters"].is_object()) {
				sourceParameters = sourceData["parameters"];
			}
		} catch (const nlohmann::json::exception&) {
			draft_.createMessage = "取り込み元のMaterialの設定形式が不正です";
			return false;
		}
	}

	// 全成果物の識別子をファイルへ書く前に揃える
	std::vector<AssetDocumentChange> changes;
	changes.reserve(createMeshTransparent ? 5 : 3);
	const auto prepareAsset = [&](const std::string& logicalPath, nlohmann::json data, AssetType type) {
		const auto path = assetDatabase->ResolveAssetPath(logicalPath);
		if (std::filesystem::exists(path) || std::filesystem::exists(ProjectAssetPath::MakeMetaPath(path))) {
			draft_.createMessage = "同名のアセットが存在します: " + logicalPath;
			return AssetID{};
		}
		AssetDocumentChange change;
		if (!AssetDocumentPublication::Prepare(*assetDatabase, logicalPath, type, change, draft_.createMessage)) {
			return AssetID{};
		}
		const AssetID identifier = change.metadata.guid;
		change.document = std::move(data);
		changes.emplace_back(std::move(change));
		return identifier;
	};

	// Shaderの保存候補と参照GUIDを作る
	const AssetID shaderID = prepareAsset(shaderLogical,
		MakeShaderJson(baseName, draft_.createVS, draft_.createPS, draft_.createMS, draft_.createAS, draft_.createGS,
			useMeshShader, useGeometryShader, isParticle, draft_.createPSEntry),
		AssetType::Shader);
	if (!shaderID) {
		draft_.createMessage = "shader.jsonの保存または登録に失敗しました: " + draft_.createMessage;
		return false;
	}

	// Pipelineの保存候補と参照GUIDを作る
	const AssetID pipelineShader = isParticle ? BuiltinAssets::Shaders::Particle : shaderID;
	const AssetID pipelineID = prepareAsset(pipelineLogical,
		MakePipelineJson(baseName, pipelineShader, useMeshShader, useGeometryShader, numRenderTargets, draft_.createPipeline),
		AssetType::RenderPipeline);
	if (!pipelineID) {
		draft_.createMessage = "pipeline.jsonの保存または登録に失敗しました: " + draft_.createMessage;
		return false;
	}

	// 半透明描画用のShaderとPipelineを作る
	AssetID transparentShaderID{};
	AssetID transparentPipelineID{};
	if (createMeshTransparent) {
		transparentShaderID = prepareAsset(transparentShaderLogical,
			MakeShaderJson(baseName + "Transparent", draft_.createVS, draft_.createTransparentPS, draft_.createMS,
				draft_.createAS, AssetID{}, useMeshShader, false, false, draft_.createTransparentPSEntry),
			AssetType::Shader);
		if (!transparentShaderID) {
			draft_.createMessage = "半透明shader.jsonの保存または登録に失敗しました: " + draft_.createMessage;
			return false;
		}

		transparentPipelineID = prepareAsset(transparentPipelineLogical,
			MakePipelineJson(
				baseName + "Transparent", transparentShaderID, useMeshShader, false, 1, draft_.createTransparentPipeline),
			AssetType::RenderPipeline);
		if (!transparentPipelineID) {
			draft_.createMessage = "半透明pipeline.jsonの保存または登録に失敗しました: " + draft_.createMessage;
			return false;
		}
	}

	// 同じ描画用途のMaterialからParameterを引き継ぐ
	nlohmann::json materialData = MakeMaterialJson(baseName, pipelineID, transparentPipelineID,
		isParticle ? shaderID : AssetID{}, draft_.createType, useMeshShader, useGeometryShader, draft_.createPipeline);
	if (sourceParameters.is_object()) {
		materialData["parameters"] = std::move(sourceParameters);
	}
	const AssetID materialID = prepareAsset(materialLogical, materialData, AssetType::Material);
	if (!materialID) {
		draft_.createMessage = "material.jsonの保存または登録に失敗しました: " + draft_.createMessage;
		return false;
	}

	// 文書とmetaをまとめて保存し、成功後に索引を公開する
	const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Material);
	if (!AssetDocumentPublication::Commit(*assetDatabase, changes, scope, draft_.createMessage)) {
		return false;
	}

	// 保存した参照と描画cacheを更新する
	RenderPipelineRunner* renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
	if (renderPipeline) {
		renderPipeline->ReloadAsset(*assetDatabase, shaderID);
		renderPipeline->ReloadAsset(*assetDatabase, pipelineID);
		if (transparentShaderID) {
			renderPipeline->ReloadAsset(*assetDatabase, transparentShaderID);
			renderPipeline->ReloadAsset(*assetDatabase, transparentPipelineID);
		}
		renderPipeline->ReloadAsset(*assetDatabase, materialID);
	}

	Logger::Output(LogType::Engine, "[MaterialEditorTool] Material Assetを作成しました path={}", materialLogical);
	draft_.createMessage = "作成しました " + materialLogical;
	return true;
}
