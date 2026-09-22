#include "MaterialCreationSession.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <filesystem>
#include <optional>
#include <string>

#include <json.hpp>

//============================================================================
//	MaterialCreationSession internal
//============================================================================

namespace {

	// D3D12列挙をpipeline.json用の文字列へ変換する
	template <typename T>
	std::string EnumToJsonString(T value) {

		return std::string(Engine::EnumAdapter<T>::ToString(value));
	}

	// pipeline.jsonの文字列値をD3D12列挙へ戻す、不正なら現状維持
	template <typename T>
	T EnumFromJsonString(const nlohmann::json& object, const char* key, T fallback) {

		if (!object.contains(key) || !object[key].is_string()) {
			return fallback;
		}
		return Engine::EnumAdapter<T>::FromString(object[key].get<std::string>()).value_or(fallback);
	}

	// 作成タイプをマテリアル用途へ変換する
	Engine::MaterialUsage ToMaterialUsage(Engine::MaterialCreateType type) {

		switch (type) {
		case Engine::MaterialCreateType::Mesh: return Engine::MaterialUsage::Mesh;
		case Engine::MaterialCreateType::Particle: return Engine::MaterialUsage::Particle;
		case Engine::MaterialCreateType::Sprite: return Engine::MaterialUsage::Sprite;
		case Engine::MaterialCreateType::Text: return Engine::MaterialUsage::Text;
		case Engine::MaterialCreateType::Line: return Engine::MaterialUsage::Line;
		default: return Engine::MaterialUsage::Generic;
		}
	}

	// マテリアル用途を作成タイプへ変換する
	std::optional<Engine::MaterialCreateType> ToMaterialCreateType(Engine::MaterialUsage usage) {

		switch (usage) {
		case Engine::MaterialUsage::Mesh: return Engine::MaterialCreateType::Mesh;
		case Engine::MaterialUsage::Particle: return Engine::MaterialCreateType::Particle;
		case Engine::MaterialUsage::Sprite: return Engine::MaterialCreateType::Sprite;
		case Engine::MaterialUsage::Text: return Engine::MaterialCreateType::Text;
		case Engine::MaterialUsage::Line: return Engine::MaterialCreateType::Line;
		default: return std::nullopt;
		}
	}

	// マテリアルパスが参照するShaderのステージ情報
	struct MaterialShaderReferences {

		Engine::AssetID vs{};
		Engine::AssetID ps{};
		Engine::AssetID ms{};
		Engine::AssetID as{};
		Engine::AssetID gs{};
		std::string psEntry = "main";
	};

	// Pipelineの先頭バリアントから編集可能な設定を読み込む
	bool ReadPipelineSettings(Engine::AssetDatabase& assetDatabase, Engine::AssetID pipelineID,
		Engine::PipelineCreateSettings& outSettings) {

		const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
		const nlohmann::json pipelineData = pipelinePath.empty() ?
			nlohmann::json{} : Engine::JsonAdapter::Load(pipelinePath.string(), false);
		if (!pipelineData.is_object() || !pipelineData.contains("variants") ||
			!pipelineData["variants"].is_array() || pipelineData["variants"].empty()) {
			return false;
		}

		const nlohmann::json& variant = pipelineData["variants"].front();
		if (variant.contains("rasterizer") && variant["rasterizer"].is_object()) {
			const auto& rasterizer = variant["rasterizer"];
			outSettings.fillMode = EnumFromJsonString(rasterizer, "fillMode", outSettings.fillMode);
			outSettings.cullMode = EnumFromJsonString(rasterizer, "cullMode", outSettings.cullMode);
			outSettings.frontCounterClockwise = rasterizer.value("frontCounterClockwise", outSettings.frontCounterClockwise);
			outSettings.depthClipEnable = rasterizer.value("depthClipEnable", outSettings.depthClipEnable);
		}
		if (variant.contains("depthStencil") && variant["depthStencil"].is_object()) {
			const auto& depthStencil = variant["depthStencil"];
			outSettings.depthEnable = depthStencil.value("depthEnable", outSettings.depthEnable);
			outSettings.depthWriteMask = EnumFromJsonString(depthStencil, "depthWriteMask", outSettings.depthWriteMask);
			outSettings.depthFunc = EnumFromJsonString(depthStencil, "depthFunc", outSettings.depthFunc);
			outSettings.stencilEnable = depthStencil.value("stencilEnable", outSettings.stencilEnable);
		}
		if (variant.contains("staticSamplers") && variant["staticSamplers"].is_array() &&
			!variant["staticSamplers"].empty()) {
			const auto& sampler = variant["staticSamplers"].front();
			outSettings.samplerFilter = EnumFromJsonString(sampler, "filter", outSettings.samplerFilter);
			outSettings.samplerAddressU = EnumFromJsonString(sampler, "addressU", outSettings.samplerAddressU);
			outSettings.samplerAddressV = EnumFromJsonString(sampler, "addressV", outSettings.samplerAddressV);
			outSettings.samplerAddressW = EnumFromJsonString(sampler, "addressW", outSettings.samplerAddressW);
			outSettings.samplerComparison = EnumFromJsonString(sampler, "comparisonFunc", outSettings.samplerComparison);
			outSettings.samplerBorderColor = EnumFromJsonString(sampler, "borderColor", outSettings.samplerBorderColor);
			outSettings.samplerMaxAnisotropy = sampler.value("maxAnisotropy", outSettings.samplerMaxAnisotropy);
			outSettings.samplerMipLODBias = sampler.value("mipLODBias", outSettings.samplerMipLODBias);
			outSettings.samplerMinLOD = sampler.value("minLOD", outSettings.samplerMinLOD);
			outSettings.samplerMaxLOD = sampler.value("maxLOD", outSettings.samplerMaxLOD);
		}
		return true;
	}

	// マテリアルパスから実際に使うShaderとステージ参照を読み込む
	bool ReadShaderReferences(Engine::AssetDatabase& assetDatabase, const nlohmann::json& pass,
		MaterialShaderReferences& outReferences) {

		Engine::AssetID shaderID = Engine::ParseAssetID(pass, "shaderOverride");
		if (!shaderID) {
			const Engine::AssetID pipelineID = Engine::ParseAssetID(pass, "pipeline");
			const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
			const nlohmann::json pipelineData = pipelinePath.empty() ?
				nlohmann::json{} : Engine::JsonAdapter::Load(pipelinePath.string(), false);
			if (!pipelineData.is_object() || !pipelineData.contains("variants") ||
				!pipelineData["variants"].is_array() || pipelineData["variants"].empty()) {
				return false;
			}
			shaderID = Engine::ParseAssetID(pipelineData["variants"].front(), "shader");
		}
		const std::filesystem::path shaderPath = assetDatabase.ResolveFullPath(shaderID);
		const nlohmann::json shaderData = shaderPath.empty() ?
			nlohmann::json{} : Engine::JsonAdapter::Load(shaderPath.string(), false);
		if (!shaderData.is_object() || !shaderData.contains("stages") || !shaderData["stages"].is_array()) {
			return false;
		}

		outReferences = MaterialShaderReferences{};
		for (const auto& stageData : shaderData["stages"]) {
			const std::string stage = stageData.value("stage", std::string{});
			const Engine::AssetID hlsl = Engine::ParseAssetID(stageData, "file");
			if (stage == "VS") {
				outReferences.vs = hlsl;
			} else if (stage == "PS") {
				outReferences.ps = hlsl;
				outReferences.psEntry = stageData.value("entry", std::string("main"));
			} else if (stage == "MS") {
				outReferences.ms = hlsl;
			} else if (stage == "AS") {
				outReferences.as = hlsl;
			} else if (stage == "GS") {
				outReferences.gs = hlsl;
			}
		}
		return static_cast<bool>(outReferences.ps);
	}

	// 1ステージ分のjsonを作る、fileはhlslのGUID参照
	nlohmann::json MakeStageJson(const char* stage, Engine::AssetID hlsl,
		const char* entry, const char* profile) {

		return nlohmann::json{
			{ "stage", stage },
			{ "file", Engine::ToAssetReferenceJson(hlsl) },
			{ "entry", entry },
			{ "profile", profile },
		};
	}

	// shader.jsonを作る、Mesh以外はMS/ASを含めずLineはGSを含める
	nlohmann::json MakeShaderJson(const std::string& name, Engine::AssetID vs, Engine::AssetID ps,
		Engine::AssetID ms, Engine::AssetID as, Engine::AssetID gs, bool includeMeshStages,
		bool includeGeometryStage, bool pixelOnly, const std::string& pixelEntry) {

		nlohmann::json stages = nlohmann::json::array();
		if (!pixelOnly) {
			stages.push_back(MakeStageJson("VS", vs, "main", "vs_6_6"));
		}
		if (includeMeshStages && as) {
			stages.push_back(MakeStageJson("AS", as, "main", "as_6_6"));
		}
		if (includeMeshStages && ms) {
			stages.push_back(MakeStageJson("MS", ms, "main", "ms_6_6"));
		}
		if (includeGeometryStage && gs) {
			stages.push_back(MakeStageJson("GS", gs, "main", "gs_6_6"));
		}
		stages.push_back(MakeStageJson("PS", ps, pixelEntry.c_str(), "ps_6_6"));

		return nlohmann::json{ { "name", name + "Shader" }, { "stages", stages } };
	}

	// 1バリアント分のpipeline.jsonを作る
	nlohmann::json MakePipelineVariantJson(Engine::AssetID shaderID, const char* kind,
		const char* pipelineType, bool useGeometryShader, bool requiresMeshShader,
		int numRenderTargets, const Engine::PipelineCreateSettings& settings) {

		const char* topology = useGeometryShader ?
			"D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE" : "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE";

		nlohmann::json variant{
			{ "kind", kind },
			{ "pipelineType", pipelineType },
			{ "shader", Engine::ToAssetReferenceJson(shaderID) },
			{ "numRenderTargets", numRenderTargets },
			{ "dynamicRenderTargetFormats", true },
			{ "dsvFormat", "DXGI_FORMAT_UNKNOWN" },
			{ "topologyType", topology },
			{ "rasterizer", {
				{ "fillMode", EnumToJsonString(settings.fillMode) },
				{ "cullMode", EnumToJsonString(settings.cullMode) },
				{ "frontCounterClockwise", settings.frontCounterClockwise },
				{ "depthClipEnable", settings.depthClipEnable },
			} },
			{ "depthStencil", {
				{ "depthEnable", settings.depthEnable },
				{ "depthWriteMask", EnumToJsonString(settings.depthWriteMask) },
				{ "depthFunc", EnumToJsonString(settings.depthFunc) },
				{ "stencilEnable", settings.stencilEnable },
			} },
			{ "staticSamplers", nlohmann::json::array({
				{
					{ "shaderRegister", 0 },
					{ "registerSpace", 0 },
					{ "filter", EnumToJsonString(settings.samplerFilter) },
					{ "addressU", EnumToJsonString(settings.samplerAddressU) },
					{ "addressV", EnumToJsonString(settings.samplerAddressV) },
					{ "addressW", EnumToJsonString(settings.samplerAddressW) },
					{ "comparisonFunc", EnumToJsonString(settings.samplerComparison) },
					{ "borderColor", EnumToJsonString(settings.samplerBorderColor) },
					{ "maxAnisotropy", settings.samplerMaxAnisotropy },
					{ "mipLODBias", settings.samplerMipLODBias },
					{ "minLOD", settings.samplerMinLOD },
					{ "maxLOD", settings.samplerMaxLOD },
					{ "shaderVisibility", "D3D12_SHADER_VISIBILITY_PIXEL" },
				}
			}) },
		};
		if (requiresMeshShader) {
			variant["requiresMeshShader"] = true;
		}
		return variant;
	}

	// pipeline.jsonを作る、Mesh Shader使用時も頂点シェーダーへフォールバック可能にする
	nlohmann::json MakePipelineJson(const std::string& name, Engine::AssetID shaderID,
		bool useMeshShader, bool useGeometryShader, int numRenderTargets, const Engine::PipelineCreateSettings& settings) {

		nlohmann::json variants = nlohmann::json::array();
		if (useMeshShader) {

			variants.push_back(MakePipelineVariantJson(shaderID, "GraphicsMesh", "Mesh", false,
				true, numRenderTargets, settings));
			variants.push_back(MakePipelineVariantJson(shaderID, "GraphicsVertex", "Vertex", false,
				false, numRenderTargets, settings));
		} else {

			variants.push_back(MakePipelineVariantJson(shaderID,
				useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex",
				useGeometryShader ? "Geometry" : "Vertex", useGeometryShader,
				false, numRenderTargets, settings));
		}
		return nlohmann::json{ { "name", name + "Pipeline" }, { "variants", std::move(variants) } };
	}

	// material.jsonを作る、domain/passKindはタイプで決める
	nlohmann::json MakeMaterialJson(const std::string& name, Engine::AssetID pipelineID,
		Engine::AssetID transparentPipelineID, Engine::AssetID shaderOverride, Engine::MaterialCreateType type,
		bool useMeshShader, bool useGeometryShader,
		const Engine::PipelineCreateSettings& settings) {

		// Mesh/Particle/Lineは3DワールドなのでSurface、Sprite/TextはUI
		const bool surfaceDomain = (type == Engine::MaterialCreateType::Mesh) ||
			(type == Engine::MaterialCreateType::Particle) ||
			(type == Engine::MaterialCreateType::Line);
		const char* domain = surfaceDomain ? "Surface" : "UI";
		const char* preferredVariant = useMeshShader ? "GraphicsMesh" : (useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex");
		auto makePass = [&](const char* passKind, Engine::AssetID passPipelineID) {
			nlohmann::json pass{
				{ "passKind", passKind },
				{ "pipeline", Engine::ToAssetReferenceJson(passPipelineID) },
				{ "preferredVariant", preferredVariant },
			};
			if (shaderOverride) {
				pass["shaderOverride"] = Engine::ToAssetReferenceJson(shaderOverride);
			}
			return pass;
		};

		nlohmann::json passes = nlohmann::json::array();
		if (type == Engine::MaterialCreateType::Mesh) {
			passes.push_back(makePass("Draw", pipelineID));
			if (transparentPipelineID) {
				passes.push_back(makePass("Transparent", transparentPipelineID));
			}
		} else {
			passes.push_back(makePass(type == Engine::MaterialCreateType::Particle ? "Transparent" : "Draw", pipelineID));
		}

		return nlohmann::json{
			{ "name", name },
			{ "domain", domain },
			{ "usage", Engine::EnumAdapter<Engine::MaterialUsage>::ToString(ToMaterialUsage(type)) },
			{ "renderState", {
				{ "overridesRenderer", true },
				{ "surfaceMode", Engine::EnumAdapter<Engine::MaterialSurfaceMode>::ToString(
					settings.surfaceMode) },
				{ "phase", std::string(Engine::ToString(
					Engine::ResolveMaterialRenderPhase(settings.surfaceMode))) },
				{ "blendMode", Engine::EnumAdapter<Engine::BlendMode>::ToString(settings.blendMode) },
			} },
			{ "passes", std::move(passes) },
			{ "parameters", nlohmann::json::object() },
		};
	}
}

void Engine::MaterialCreationSession::ApplyTypeDefaults(MaterialCreateType type) {

	PipelineCreateSettings settings{};
	if (type == MaterialCreateType::Mesh) {

		// メッシュは深度テスト書き込み有効で裏面カリングする
		settings.cullMode = D3D12_CULL_MODE_BACK;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Particle) {

		// Particleは半透明描画を前提に深度テストのみ有効にする
		settings.cullMode = D3D12_CULL_MODE_NONE;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.surfaceMode = MaterialSurfaceMode::Transparent;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Sprite) {

		// スプライトは深度無効でラップサンプリング
		settings.surfaceMode = MaterialSurfaceMode::Transparent;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Line) {

		// ラインは3Dワールドに描くので深度テスト有効で裏面カリングしない
		settings.cullMode = D3D12_CULL_MODE_NONE;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	} else {

		// テキストは深度無効でクランプサンプリング
		settings.surfaceMode = MaterialSurfaceMode::Transparent;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	draft_.createPipeline = settings;
	draft_.createTransparentPipeline = settings;
	draft_.createTransparentPipeline.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	draft_.createTransparentPass = type == MaterialCreateType::Mesh;

	// Mesh以外はメッシュ系シェーダーを使わないのでクリアする
	if (type != MaterialCreateType::Mesh) {

		draft_.createMS = AssetID{};
		draft_.createAS = AssetID{};
		draft_.createTransparentPS = AssetID{};
	}
	// Line以外はジオメトリシェーダーを使わないのでクリアする
	if (type != MaterialCreateType::Line) {

		draft_.createGS = AssetID{};
	}
}

void Engine::MaterialCreationSession::LoadPipelineSettingsFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
	if (materialPath.empty()) {
		draft_.createMessage = "マテリアルファイルが見つかりません";
		return;
	}

	const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
	draft_.createSourceUsesShaderGraph = static_cast<bool>(
		ParseAssetID(materialData, "shaderGraph"));
	if (!materialData.is_object() || !materialData.contains("passes") ||
		!materialData["passes"].is_array() || materialData["passes"].empty()) {
		draft_.createMessage = "マテリアルにパスがありません";
		return;
	}

	const MaterialUsage usage = EnumAdapter<MaterialUsage>::FromString(
		materialData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
	if (const std::optional<MaterialCreateType> type = ToMaterialCreateType(usage)) {
		draft_.createType = *type;
		ApplyTypeDefaults(draft_.createType);
	}

	// DrawとTransparentのPipeline設定を別々に取り込む
	AssetID drawPipelineID{};
	AssetID transparentPipelineID{};
	for (const auto& pass : materialData["passes"]) {
		const std::string passKind = pass.value("passKind", std::string{});
		if (passKind == "Draw") {
			drawPipelineID = ParseAssetID(pass, "pipeline");
		} else if (passKind == "Transparent") {
			transparentPipelineID = ParseAssetID(pass, "pipeline");
		}
	}
	if (const auto renderState = materialData.find("renderState");
		renderState != materialData.end() && renderState->is_object()) {
		const RenderPhase legacyPhase = RenderPhaseFromString(
			renderState->value("phase", "Opaque"), RenderPhase::Opaque);
		draft_.createPipeline.surfaceMode =
			EnumAdapter<MaterialSurfaceMode>::FromString(
				renderState->value("surfaceMode",
					legacyPhase == RenderPhase::Transparent ?
					"Transparent" : "Opaque")).
			value_or(MaterialSurfaceMode::Opaque);
		draft_.createPipeline.blendMode = EnumAdapter<BlendMode>::FromString(
			renderState->value("blendMode", "Normal")).value_or(BlendMode::Normal);
	}
	if (!drawPipelineID) {
		drawPipelineID = ParseAssetID(materialData["passes"].front(), "pipeline");
	}
	if (!drawPipelineID || !ReadPipelineSettings(assetDatabase, drawPipelineID, draft_.createPipeline)) {
		draft_.createMessage = "マテリアルからpipeline参照を取得できません";
		return;
	}
	draft_.createTransparentPass = draft_.createType == MaterialCreateType::Mesh && static_cast<bool>(transparentPipelineID);
	if (draft_.createTransparentPass && !ReadPipelineSettings(assetDatabase,
		transparentPipelineID, draft_.createTransparentPipeline)) {
		draft_.createMessage = "半透明pipelineにvariantがありません";
		return;
	}

	draft_.createMessage = "パイプライン設定を取り込みました";
}

void Engine::MaterialCreationSession::LoadShadersFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
	if (materialPath.empty()) {
		return;
	}
	const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
	if (!materialData.is_object() || !materialData.contains("passes") ||
		!materialData["passes"].is_array() || materialData["passes"].empty()) {
		return;
	}

	// 通常描画と半透明描画のパスを分けて探す
	const nlohmann::json* primaryPass = nullptr;
	const nlohmann::json* transparentPass = nullptr;
	for (const auto& pass : materialData["passes"]) {
		const std::string passKind = pass.value("passKind", std::string{});
		if (passKind == "Draw" && !primaryPass) {
			primaryPass = &pass;
		} else if (passKind == "Transparent") {
			transparentPass = &pass;
		}
	}
	if (!primaryPass) {
		primaryPass = transparentPass ? transparentPass : &materialData["passes"].front();
	}

	MaterialShaderReferences primaryReferences{};
	if (!ReadShaderReferences(assetDatabase, *primaryPass, primaryReferences)) {
		return;
	}
	draft_.createVS = primaryReferences.vs;
	draft_.createPS = primaryReferences.ps;
	draft_.createMS = primaryReferences.ms;
	draft_.createAS = primaryReferences.as;
	draft_.createGS = primaryReferences.gs;
	draft_.createPSEntry = primaryReferences.psEntry;

	if (draft_.createType == MaterialCreateType::Mesh) {
		draft_.createTransparentPass = transparentPass != nullptr;
		draft_.createTransparentPS = AssetID{};
		if (transparentPass) {
			MaterialShaderReferences transparentReferences{};
			if (ReadShaderReferences(assetDatabase, *transparentPass, transparentReferences)) {
				draft_.createTransparentPS = transparentReferences.ps;
				draft_.createTransparentPSEntry = transparentReferences.psEntry;
			}
		}
	}

	draft_.createMessage = "シェーダーを取り込みました";
}

bool Engine::MaterialCreationSession::CreateMaterialAssets(const EditorToolContext& context) {

	draft_.createMessage.clear();

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase) {
		draft_.createMessage = "AssetDatabaseが利用できません";
		return false;
	}
	if ((draft_.createType != MaterialCreateType::Particle && !draft_.createVS) || !draft_.createPS) {
		draft_.createMessage = draft_.createType == MaterialCreateType::Particle ?
			"PixelShaderは必須です" : "VertexShaderとPixelShaderは必須です";
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
	// LineはGSで太線へ展開するのでGeometryShaderも必須
	if (draft_.createType == MaterialCreateType::Line && !draft_.createGS) {
		draft_.createMessage = "LineはGeometryShaderも必須です";
		return false;
	}

	// 入力パスの前後区切りを整理してファイル名を取り出す
	std::string relativePath = draft_.createRelativePath;
	while (!relativePath.empty() && (relativePath.front() == '/' || relativePath.front() == '\\')) {
		relativePath.erase(relativePath.begin());
	}
	while (!relativePath.empty() && (relativePath.back() == '/' || relativePath.back() == '\\')) {
		relativePath.pop_back();
	}
	if (relativePath.empty()) {
		draft_.createMessage = "出力パスを入力してください";
		return false;
	}
	std::string baseName = relativePath;
	if (const size_t pos = relativePath.find_last_of("/\\"); pos != std::string::npos) {
		baseName = relativePath.substr(pos + 1);
	}

	const bool useMeshShader = (draft_.createType == MaterialCreateType::Mesh) && static_cast<bool>(draft_.createMS);
	const bool useGeometryShader = (draft_.createType == MaterialCreateType::Line);
	const bool isParticle = draft_.createType == MaterialCreateType::Particle;
	const int numRenderTargets = draft_.createType == MaterialCreateType::Mesh ? 3 : 1;

	// 描画パス別のShader/PipelineとMaterialを同じ階層へ書き出す
	const std::string shaderLogical = "GameAssets/Materials/" + relativePath + ".shader.json";
	const std::string pipelineLogical = "GameAssets/Materials/" + relativePath + ".pipeline.json";
	const std::string transparentShaderLogical = "GameAssets/Materials/" + relativePath + "Transparent.shader.json";
	const std::string transparentPipelineLogical = "GameAssets/Materials/" + relativePath + "Transparent.pipeline.json";
	const std::string materialLogical = "GameAssets/Materials/" + relativePath + ".material.json";

	const std::filesystem::path shaderPath = assetDatabase->ResolveAssetPath(shaderLogical);
	std::error_code ec;
	std::filesystem::create_directories(shaderPath.parent_path(), ec);

	// shaderを書き出して登録し、得たGUIDをpipelineが参照する
	JsonAdapter::Save(shaderPath.string(),
		MakeShaderJson(baseName, draft_.createVS, draft_.createPS, draft_.createMS, draft_.createAS, draft_.createGS,
			useMeshShader, useGeometryShader, isParticle, draft_.createPSEntry));
	const AssetID shaderID = assetDatabase->ImportOrGet(shaderLogical, AssetType::Shader);
	if (!shaderID) {
		draft_.createMessage = "shader.jsonの登録に失敗しました";
		return false;
	}

	// pipelineを書き出して登録し、得たGUIDをmaterialが参照する
	const AssetID pipelineShader = isParticle ? BuiltinAssets::Shaders::Particle : shaderID;
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(pipelineLogical).string(),
		MakePipelineJson(baseName, pipelineShader, useMeshShader, useGeometryShader, numRenderTargets, draft_.createPipeline));
	const AssetID pipelineID = assetDatabase->ImportOrGet(pipelineLogical, AssetType::RenderPipeline);
	if (!pipelineID) {
		draft_.createMessage = "pipeline.jsonの登録に失敗しました";
		return false;
	}

	// 半透明はForward用PSと1枚のRenderTargetを使う別Pipelineとして生成する
	AssetID transparentShaderID{};
	AssetID transparentPipelineID{};
	if (createMeshTransparent) {
		JsonAdapter::Save(assetDatabase->ResolveAssetPath(transparentShaderLogical).string(),
			MakeShaderJson(baseName + "Transparent", draft_.createVS, draft_.createTransparentPS, draft_.createMS, draft_.createAS, AssetID{},
				useMeshShader, false, false, draft_.createTransparentPSEntry));
		transparentShaderID = assetDatabase->ImportOrGet(transparentShaderLogical, AssetType::Shader);
		if (!transparentShaderID) {
			draft_.createMessage = "半透明shader.jsonの登録に失敗しました";
			return false;
		}

		JsonAdapter::Save(assetDatabase->ResolveAssetPath(transparentPipelineLogical).string(),
			MakePipelineJson(baseName + "Transparent", transparentShaderID,
				useMeshShader, false, 1, draft_.createTransparentPipeline));
		transparentPipelineID = assetDatabase->ImportOrGet(
			transparentPipelineLogical, AssetType::RenderPipeline);
		if (!transparentPipelineID) {
			draft_.createMessage = "半透明pipeline.jsonの登録に失敗しました";
			return false;
		}
	}

	// materialを書き出して登録する、同用途の取り込み元があればパラメータも引き継ぐ
	nlohmann::json materialData = MakeMaterialJson(baseName, pipelineID, transparentPipelineID,
		isParticle ? shaderID : AssetID{}, draft_.createType, useMeshShader, useGeometryShader,
		draft_.createPipeline);
	if (draft_.createSourceMaterial) {

		const std::filesystem::path sourcePath = assetDatabase->ResolveFullPath(draft_.createSourceMaterial);
		const nlohmann::json sourceData = sourcePath.empty() ?
			nlohmann::json{} : JsonAdapter::Load(sourcePath.string(), false);
		const MaterialUsage sourceUsage = EnumAdapter<MaterialUsage>::FromString(
			sourceData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
		if (sourceUsage == ToMaterialUsage(draft_.createType) && sourceData.contains("parameters") &&
			sourceData["parameters"].is_object()) {

			materialData["parameters"] = sourceData["parameters"];
		}
	}
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(materialLogical).string(), materialData);
	const AssetID materialID = assetDatabase->ImportOrGet(materialLogical, AssetType::Material);
	if (!materialID) {
		draft_.createMessage = "material.jsonの登録に失敗しました";
		return false;
	}

	// 上書き時も依存関係と描画キャッシュを現在のファイル内容へ同期する
	RenderPipelineRunner* renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
	if (renderPipeline) {
		renderPipeline->ReloadAsset(*assetDatabase, shaderID);
		renderPipeline->ReloadAsset(*assetDatabase, pipelineID);
		if (transparentShaderID) {
			renderPipeline->ReloadAsset(*assetDatabase, transparentShaderID);
			renderPipeline->ReloadAsset(*assetDatabase, transparentPipelineID);
		}
		renderPipeline->ReloadAsset(*assetDatabase, materialID);
	} else {
		assetDatabase->RefreshDependencies(shaderID);
		assetDatabase->RefreshDependencies(pipelineID);
		if (transparentShaderID) {
			assetDatabase->RefreshDependencies(transparentShaderID);
			assetDatabase->RefreshDependencies(transparentPipelineID);
		}
		assetDatabase->RefreshDependencies(materialID);
	}

	Logger::Output(LogType::Engine, "[MaterialEditorTool] Material Assetを作成しました path={}", materialLogical);
	draft_.createMessage = "作成しました " + materialLogical;
	return true;
}
