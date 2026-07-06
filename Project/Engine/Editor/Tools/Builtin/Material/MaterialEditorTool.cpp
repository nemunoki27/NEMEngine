#include "MaterialEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// imgui
#include <imgui.h>

// c++
#include <filesystem>
#include <string>
// json
#include <json.hpp>

//============================================================================
//	MaterialEditorTool internal
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

	// 1ステージ分のjsonを作る、fileはhlslのGUID参照
	nlohmann::json MakeStageJson(const char* stage, Engine::AssetID hlsl, const char* profile) {

		return nlohmann::json{
			{ "stage", stage },
			{ "file", Engine::ToAssetReferenceJson(hlsl) },
			{ "entry", "main" },
			{ "profile", profile },
		};
	}

	// shader.jsonを作る、Mesh以外はMS/ASを含めずLineはGSを含める
	nlohmann::json MakeShaderJson(const std::string& name, Engine::AssetID vs, Engine::AssetID ps,
		Engine::AssetID ms, Engine::AssetID as, Engine::AssetID gs, bool includeMeshStages, bool includeGeometryStage) {

		nlohmann::json stages = nlohmann::json::array();
		stages.push_back(MakeStageJson("VS", vs, "vs_6_6"));
		if (includeMeshStages && as) {
			stages.push_back(MakeStageJson("AS", as, "as_6_6"));
		}
		if (includeMeshStages && ms) {
			stages.push_back(MakeStageJson("MS", ms, "ms_6_6"));
		}
		if (includeGeometryStage && gs) {
			stages.push_back(MakeStageJson("GS", gs, "gs_6_6"));
		}
		stages.push_back(MakeStageJson("PS", ps, "ps_6_6"));

		return nlohmann::json{ { "name", name + "Shader" }, { "stages", stages } };
	}

	// pipeline.jsonを作る、kind/pipelineType/numRenderTargets等はエンジン仕様で確定する
	nlohmann::json MakePipelineJson(const std::string& name, Engine::AssetID shaderID,
		bool useMeshShader, bool useGeometryShader, int numRenderTargets, const Engine::PipelineCreateSettings& settings) {

		const char* kind = useMeshShader ? "GraphicsMesh" : (useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex");
		const char* pipelineType = useMeshShader ? "Mesh" : (useGeometryShader ? "Geometry" : "Vertex");
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
					{ "addressU", EnumToJsonString(settings.samplerAddress) },
					{ "addressV", EnumToJsonString(settings.samplerAddress) },
					{ "addressW", EnumToJsonString(settings.samplerAddress) },
					{ "comparisonFunc", "D3D12_COMPARISON_FUNC_ALWAYS" },
					{ "shaderVisibility", "D3D12_SHADER_VISIBILITY_PIXEL" },
				}
			}) },
		};
		if (useMeshShader) {
			variant["requiresMeshShader"] = true;
		}
		return nlohmann::json{ { "name", name + "Pipeline" }, { "variants", nlohmann::json::array({ variant }) } };
	}

	// material.jsonを作る、domain/passKindはタイプで決める
	nlohmann::json MakeMaterialJson(const std::string& name, Engine::AssetID pipelineID,
		Engine::MaterialCreateType type, bool useMeshShader, bool useGeometryShader) {

		// Mesh/Line/FillFaceMeshは3DワールドなのでSurface、Sprite/TextはUI
		const bool surfaceDomain = (type == Engine::MaterialCreateType::Mesh) ||
			(type == Engine::MaterialCreateType::Line) || (type == Engine::MaterialCreateType::FillFaceMesh);
		const char* domain = surfaceDomain ? "Surface" : "UI";
		const char* preferredVariant = useMeshShader ? "GraphicsMesh" : (useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex");

		return nlohmann::json{
			{ "name", name },
			{ "domain", domain },
			{ "passes", nlohmann::json::array({
				{
					{ "passKind", "Draw" },
					{ "pipeline", Engine::ToAssetReferenceJson(pipelineID) },
					{ "preferredVariant", preferredVariant },
				}
			}) },
			{ "parameters", nlohmann::json::object() },
		};
	}
}

//============================================================================
//	MaterialEditorTool classMethods
//============================================================================
void Engine::MaterialEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::MaterialEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::MaterialEditorTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Material", &openWindow_)) {
		ImGui::End();
		return;
	}

	// このウィンドウだけ文字を少し小さくする、最後に戻す
	ImGui::SetWindowFontScale(0.8f);

	// タイプ既定値は初回だけ現タイプで埋める
	if (!typeDefaultsInitialized_) {
		ApplyTypeDefaults(createType_);
		typeDefaultsInitialized_ = true;
	}

	// 役割ごとにタブで分ける
	if (ImGui::BeginTabBar("##MaterialTabs")) {

		if (ImGui::BeginTabItem("デフォルトマテリアル設定")) {

			DrawDefaultMaterialSection(context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("マテリアル作成")) {

			DrawCreateMaterialSection(context);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	// 他ウィンドウへ影響しないよう倍率を戻す
	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

void Engine::MaterialEditorTool::DrawDefaultMaterialSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	DefaultMaterialSettings& settings = DefaultMaterialSettings::GetInstance();

	// マテリアルを差し替えたら即保存する、空に戻すとbuiltinへ戻る
	// 未設定でも実効のbuiltin名をDefault表示するため設定済みのbuiltin込み実効値を渡す
	auto drawSlot = [&](const char* label, AssetID current, AssetID effectiveDefault, auto&& setter) {

		AssetID value = current;
		AssetEditSetting setting{};
		setting.defaultAssetID = effectiveDefault;
		if (MyGUI::AssetReferenceField(label, value, assetDatabase, { AssetType::Material }, setting).valueChanged) {

			setter(value);
			settings.Save();
		}
		};

	drawSlot("Mesh", settings.GetMesh(), settings.GetMeshOrBuiltin(),
		[&](AssetID id) { settings.SetMesh(id); });
	drawSlot("Sprite", settings.GetSprite(), settings.GetSpriteOrBuiltin(),
		[&](AssetID id) { settings.SetSprite(id); });
	drawSlot("Text", settings.GetText(), settings.GetTextOrBuiltin(),
		[&](AssetID id) { settings.SetText(id); });
	drawSlot("Line", settings.GetLine(), settings.GetLineOrBuiltin(),
		[&](AssetID id) { settings.SetLine(id); });
	drawSlot("FillFaceMesh", settings.GetFillMesh(), settings.GetFillMeshOrBuiltin(),
		[&](AssetID id) { settings.SetFillMesh(id); });
	drawSlot("Primitive", settings.GetPrimitive(), settings.GetPrimitiveOrBuiltin(),
		[&](AssetID id) { settings.SetPrimitive(id); });
	drawSlot("Primitive2D", settings.GetPrimitive2D(), settings.GetPrimitive2DOrBuiltin(),
		[&](AssetID id) { settings.SetPrimitive2D(id); });
}

void Engine::MaterialEditorTool::DrawCreateMaterialSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	// 作成対象タイプ、切り替えで既定設定を入れ直す
	if (MyGUI::EnumCombo("タイプ", createType_).valueChanged) {

		ApplyTypeDefaults(createType_);
	}

	// 対象シェーダー、VS/PSは必須でMS/ASはMeshのときだけ任意で受け付ける
	ImGui::Separator();
	{
		AssetEditSetting setting{};
		MyGUI::AssetReferenceField("VertexShader 必須", createVS_, assetDatabase, { AssetType::Shader }, setting);
		MyGUI::AssetReferenceField("PixelShader 必須", createPS_, assetDatabase, { AssetType::Shader }, setting);
		if (createType_ == MaterialCreateType::Mesh) {

			MyGUI::AssetReferenceField("MeshShader 任意", createMS_, assetDatabase, { AssetType::Shader }, setting);
			MyGUI::AssetReferenceField("AmplificationShader 任意", createAS_, assetDatabase, { AssetType::Shader }, setting);
		}
		// Lineは太線展開のGeometryShaderが必須
		if (createType_ == MaterialCreateType::Line) {

			MyGUI::AssetReferenceField("GeometryShader 必須", createGS_, assetDatabase, { AssetType::Shader }, setting);
		}

		// 既存マテリアルをドロップするとそのパイプライン設定を下の編集欄へ取り込む
		AssetID sourceMaterial = createSourceMaterial_;
		if (MyGUI::AssetReferenceField("設定取り込み元 任意", sourceMaterial,
			assetDatabase, { AssetType::Material }, setting).valueChanged) {

			createSourceMaterial_ = sourceMaterial;
			if (sourceMaterial && assetDatabase) {

				LoadPipelineSettingsFromMaterial(*assetDatabase, sourceMaterial);
				if (createImportShaders_) {
					LoadShadersFromMaterial(*assetDatabase, sourceMaterial);
				}
			}
		}

		// 参照シェーダーも取り込む、後からONにしても取り込み元から引き直す
		if (MyGUI::Checkbox("参照シェーダーを復元", createImportShaders_)) {
			if (createImportShaders_ && createSourceMaterial_ && assetDatabase) {
				LoadShadersFromMaterial(*assetDatabase, createSourceMaterial_);
			}
		}
	}

	// パイプライン設定、エンジン仕様で固定の項目は編集不可でテキスト表示する
	ImGui::SeparatorText("ラスタライズ設定");
	{
		MyGUI::EnumCombo("塗りモード", createPipeline_.fillMode);
		MyGUI::EnumCombo("カリング", createPipeline_.cullMode);
		MyGUI::Checkbox("前面反時計回り", createPipeline_.frontCounterClockwise);
		MyGUI::Checkbox("深度クリップ", createPipeline_.depthClipEnable);
	}
	ImGui::SeparatorText("深度設定");
	{
		MyGUI::Checkbox("深度テスト", createPipeline_.depthEnable);
		MyGUI::EnumCombo("深度書込み", createPipeline_.depthWriteMask);
		MyGUI::EnumCombo("深度比較", createPipeline_.depthFunc);
		MyGUI::Checkbox("ステンシル", createPipeline_.stencilEnable);
	}
	ImGui::SeparatorText("サンプラー設定");
	{
		MyGUI::EnumCombo("フィルタ", createPipeline_.samplerFilter);
		MyGUI::EnumCombo("アドレスモード", createPipeline_.samplerAddress);
	}

	// 出力先、GameAssets/Materials/固定でそれ以降をファイル名込みで入力する
	ImGui::SeparatorText("作成");
	MyGUI::InputText("GameAssets/Materials/", createRelativePath_);

	const bool canCreate = static_cast<bool>(createVS_) && static_cast<bool>(createPS_) && !createRelativePath_.empty();
	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		CreateMaterialAssets(context);
	}
	ImGui::EndDisabled();

	if (!createMessage_.empty()) {
		ImGui::TextWrapped("%s", createMessage_.c_str());
	}
}

void Engine::MaterialEditorTool::ApplyTypeDefaults(MaterialCreateType type) {

	PipelineCreateSettings settings{};
	if (type == MaterialCreateType::Mesh) {

		// メッシュは深度テスト書き込み有効で裏面カリングする
		settings.cullMode = D3D12_CULL_MODE_BACK;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Sprite) {

		// スプライトは深度無効でラップサンプリング
		settings.samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Line) {

		// ラインは3Dワールドに描くので深度テスト有効で裏面カリングしない
		settings.cullMode = D3D12_CULL_MODE_NONE;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	} else if (type == MaterialCreateType::FillFaceMesh) {

		// 面は両面表示で深度テスト書き込み有効、GBufferへ書く
		settings.cullMode = D3D12_CULL_MODE_NONE;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else {

		// テキストは深度無効でクランプサンプリング
		settings.samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	createPipeline_ = settings;

	// Mesh以外はメッシュ系シェーダーを使わないのでクリアする
	if (type != MaterialCreateType::Mesh) {

		createMS_ = AssetID{};
		createAS_ = AssetID{};
	}
	// Line以外はジオメトリシェーダーを使わないのでクリアする
	if (type != MaterialCreateType::Line) {

		createGS_ = AssetID{};
	}
}

void Engine::MaterialEditorTool::LoadPipelineSettingsFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
	if (materialPath.empty()) {
		createMessage_ = "マテリアルファイルが見つかりません";
		return;
	}

	const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
	if (!materialData.is_object() || !materialData.contains("passes") ||
		!materialData["passes"].is_array() || materialData["passes"].empty()) {
		createMessage_ = "マテリアルにパスがありません";
		return;
	}

	// Drawパス優先で参照pipelineを引き、無ければ先頭パスから引く
	AssetID pipelineID{};
	for (const auto& pass : materialData["passes"]) {
		if (pass.value("passKind", std::string{}) == "Draw") {

			pipelineID = ParseAssetID(pass, "pipeline");
			break;
		}
	}
	if (!pipelineID) {
		pipelineID = ParseAssetID(materialData["passes"].front(), "pipeline");
	}
	if (!pipelineID) {
		createMessage_ = "マテリアルからpipeline参照を取得できません";
		return;
	}

	const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
	const nlohmann::json pipelineData = pipelinePath.empty() ?
		nlohmann::json{} : JsonAdapter::Load(pipelinePath.string(), false);
	if (!pipelineData.is_object() || !pipelineData.contains("variants") ||
		!pipelineData["variants"].is_array() || pipelineData["variants"].empty()) {
		createMessage_ = "pipelineにvariantがありません";
		return;
	}

	// 先頭variantのrasterizer/depthStencil/staticSamplerを編集欄へ写す、無い項目は現状維持
	const nlohmann::json& variant = pipelineData["variants"].front();
	if (variant.contains("rasterizer") && variant["rasterizer"].is_object()) {

		const auto& rasterizer = variant["rasterizer"];
		createPipeline_.fillMode = EnumFromJsonString(rasterizer, "fillMode", createPipeline_.fillMode);
		createPipeline_.cullMode = EnumFromJsonString(rasterizer, "cullMode", createPipeline_.cullMode);
		createPipeline_.frontCounterClockwise = rasterizer.value("frontCounterClockwise", createPipeline_.frontCounterClockwise);
		createPipeline_.depthClipEnable = rasterizer.value("depthClipEnable", createPipeline_.depthClipEnable);
	}
	if (variant.contains("depthStencil") && variant["depthStencil"].is_object()) {

		const auto& depthStencil = variant["depthStencil"];
		createPipeline_.depthEnable = depthStencil.value("depthEnable", createPipeline_.depthEnable);
		createPipeline_.depthWriteMask = EnumFromJsonString(depthStencil, "depthWriteMask", createPipeline_.depthWriteMask);
		createPipeline_.depthFunc = EnumFromJsonString(depthStencil, "depthFunc", createPipeline_.depthFunc);
		createPipeline_.stencilEnable = depthStencil.value("stencilEnable", createPipeline_.stencilEnable);
	}
	if (variant.contains("staticSamplers") && variant["staticSamplers"].is_array() &&
		!variant["staticSamplers"].empty()) {

		const auto& sampler = variant["staticSamplers"].front();
		createPipeline_.samplerFilter = EnumFromJsonString(sampler, "filter", createPipeline_.samplerFilter);
		createPipeline_.samplerAddress = EnumFromJsonString(sampler, "addressU", createPipeline_.samplerAddress);
	}

	createMessage_ = "パイプライン設定を取り込みました";
}

void Engine::MaterialEditorTool::LoadShadersFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
	if (materialPath.empty()) {
		return;
	}
	const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
	if (!materialData.is_object() || !materialData.contains("passes") ||
		!materialData["passes"].is_array() || materialData["passes"].empty()) {
		return;
	}

	// Drawパス優先で参照pipelineを引く
	AssetID pipelineID{};
	for (const auto& pass : materialData["passes"]) {
		if (pass.value("passKind", std::string{}) == "Draw") {

			pipelineID = ParseAssetID(pass, "pipeline");
			break;
		}
	}
	if (!pipelineID) {
		pipelineID = ParseAssetID(materialData["passes"].front(), "pipeline");
	}
	if (!pipelineID) {
		return;
	}

	const std::filesystem::path pipelinePath = assetDatabase.ResolveFullPath(pipelineID);
	const nlohmann::json pipelineData = pipelinePath.empty() ?
		nlohmann::json{} : JsonAdapter::Load(pipelinePath.string(), false);
	if (!pipelineData.is_object() || !pipelineData.contains("variants") ||
		!pipelineData["variants"].is_array() || pipelineData["variants"].empty()) {
		return;
	}

	// 先頭variantが参照するshaderを引く
	const AssetID shaderID = ParseAssetID(pipelineData["variants"].front(), "shader");
	if (!shaderID) {
		return;
	}
	const std::filesystem::path shaderPath = assetDatabase.ResolveFullPath(shaderID);
	const nlohmann::json shaderData = shaderPath.empty() ?
		nlohmann::json{} : JsonAdapter::Load(shaderPath.string(), false);
	if (!shaderData.is_object() || !shaderData.contains("stages") || !shaderData["stages"].is_array()) {
		return;
	}

	// 無いステージが前の値で残らないよう一度クリアしてからステージ別に入れ直す
	// fileがGUID参照でないステージは解決できないので飛ばす
	createVS_ = AssetID{};
	createPS_ = AssetID{};
	createMS_ = AssetID{};
	createAS_ = AssetID{};
	for (const auto& stageData : shaderData["stages"]) {

		const std::string stage = stageData.value("stage", std::string{});
		const AssetID hlsl = ParseAssetID(stageData, "file");
		if (!hlsl) {
			continue;
		}
		if (stage == "VS") {
			createVS_ = hlsl;
		} else if (stage == "PS") {
			createPS_ = hlsl;
		} else if (stage == "MS") {
			createMS_ = hlsl;
		} else if (stage == "AS") {
			createAS_ = hlsl;
		}
	}

	createMessage_ = "シェーダーを取り込みました";
}

bool Engine::MaterialEditorTool::CreateMaterialAssets(const EditorToolContext& context) {

	createMessage_.clear();

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase) {
		createMessage_ = "AssetDatabaseが利用できません";
		return false;
	}
	if (!createVS_ || !createPS_) {
		createMessage_ = "VertexShaderとPixelShaderは必須です";
		return false;
	}
	// LineはGSで太線へ展開するのでGeometryShaderも必須
	if (createType_ == MaterialCreateType::Line && !createGS_) {
		createMessage_ = "LineはGeometryShaderも必須です";
		return false;
	}

	// 入力パスの前後区切りを整理してファイル名を取り出す
	std::string relativePath = createRelativePath_;
	while (!relativePath.empty() && (relativePath.front() == '/' || relativePath.front() == '\\')) {
		relativePath.erase(relativePath.begin());
	}
	while (!relativePath.empty() && (relativePath.back() == '/' || relativePath.back() == '\\')) {
		relativePath.pop_back();
	}
	if (relativePath.empty()) {
		createMessage_ = "出力パスを入力してください";
		return false;
	}
	std::string baseName = relativePath;
	if (const size_t pos = relativePath.find_last_of("/\\"); pos != std::string::npos) {
		baseName = relativePath.substr(pos + 1);
	}

	const bool useMeshShader = (createType_ == MaterialCreateType::Mesh) && static_cast<bool>(createMS_);
	const bool useGeometryShader = (createType_ == MaterialCreateType::Line);
	const int numRenderTargets = (createType_ == MaterialCreateType::Mesh ||
		createType_ == MaterialCreateType::FillFaceMesh) ? 3 : 1;

	// 3ファイルともGameAssets/Materials/以下の同じ階層へ同じ基底名で書き出す
	const std::string shaderLogical = "GameAssets/Materials/" + relativePath + ".shader.json";
	const std::string pipelineLogical = "GameAssets/Materials/" + relativePath + ".pipeline.json";
	const std::string materialLogical = "GameAssets/Materials/" + relativePath + ".material.json";

	const std::filesystem::path shaderPath = assetDatabase->ResolveAssetPath(shaderLogical);
	std::error_code ec;
	std::filesystem::create_directories(shaderPath.parent_path(), ec);

	// shaderを書き出して登録し、得たGUIDをpipelineが参照する
	JsonAdapter::Save(shaderPath.string(),
		MakeShaderJson(baseName, createVS_, createPS_, createMS_, createAS_, createGS_, useMeshShader, useGeometryShader));
	const AssetID shaderID = assetDatabase->ImportOrGet(shaderLogical, AssetType::Shader);
	if (!shaderID) {
		createMessage_ = "shader.jsonの登録に失敗しました";
		return false;
	}

	// pipelineを書き出して登録し、得たGUIDをmaterialが参照する
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(pipelineLogical).string(),
		MakePipelineJson(baseName, shaderID, useMeshShader, useGeometryShader, numRenderTargets, createPipeline_));
	const AssetID pipelineID = assetDatabase->ImportOrGet(pipelineLogical, AssetType::RenderPipeline);
	if (!pipelineID) {
		createMessage_ = "pipeline.jsonの登録に失敗しました";
		return false;
	}

	// materialを書き出して登録する
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(materialLogical).string(),
		MakeMaterialJson(baseName, pipelineID, createType_, useMeshShader, useGeometryShader));
	const AssetID materialID = assetDatabase->ImportOrGet(materialLogical, AssetType::Material);
	if (!materialID) {
		createMessage_ = "material.jsonの登録に失敗しました";
		return false;
	}

	Logger::Output(LogType::Engine, "[MaterialEditorTool] created material assets. path={}", materialLogical);
	createMessage_ = "作成しました " + materialLogical;
	return true;
}
