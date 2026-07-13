#include "MaterialEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>

// imgui
#include <imgui.h>

// c++
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
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

	// 作成タイプをマテリアル用途へ変換する
	Engine::MaterialUsage ToMaterialUsage(Engine::MaterialCreateType type) {

		switch (type) {
		case Engine::MaterialCreateType::Mesh: return Engine::MaterialUsage::Mesh;
		case Engine::MaterialCreateType::Particle: return Engine::MaterialUsage::Particle;
		case Engine::MaterialCreateType::Sprite: return Engine::MaterialUsage::Sprite;
		case Engine::MaterialCreateType::Text: return Engine::MaterialUsage::Text;
		case Engine::MaterialCreateType::Line: return Engine::MaterialUsage::Line;
		case Engine::MaterialCreateType::FillFaceMesh: return Engine::MaterialUsage::FillFaceMesh;
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
		case Engine::MaterialUsage::FillFaceMesh: return Engine::MaterialCreateType::FillFaceMesh;
		default: return std::nullopt;
		}
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
		Engine::AssetID ms, Engine::AssetID as, Engine::AssetID gs, bool includeMeshStages,
		bool includeGeometryStage, bool pixelOnly) {

		nlohmann::json stages = nlohmann::json::array();
		if (!pixelOnly) {
			stages.push_back(MakeStageJson("VS", vs, "vs_6_6"));
		}
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
		Engine::AssetID shaderOverride, Engine::MaterialCreateType type,
		bool useMeshShader, bool useGeometryShader) {

		// Mesh/Line/FillFaceMeshは3DワールドなのでSurface、Sprite/TextはUI
		const bool surfaceDomain = (type == Engine::MaterialCreateType::Mesh) ||
			(type == Engine::MaterialCreateType::Particle) ||
			(type == Engine::MaterialCreateType::Line) || (type == Engine::MaterialCreateType::FillFaceMesh);
		const char* domain = surfaceDomain ? "Surface" : "UI";
		const char* preferredVariant = useMeshShader ? "GraphicsMesh" : (useGeometryShader ? "GraphicsGeometry" : "GraphicsVertex");
		const char* passKind = type == Engine::MaterialCreateType::Particle ? "Transparent" : "Draw";
		nlohmann::json pass{
			{ "passKind", passKind },
			{ "pipeline", Engine::ToAssetReferenceJson(pipelineID) },
			{ "preferredVariant", preferredVariant },
		};
		if (shaderOverride) {
			pass["shaderOverride"] = Engine::ToAssetReferenceJson(shaderOverride);
		}

		return nlohmann::json{
			{ "name", name },
			{ "domain", domain },
			{ "usage", Engine::EnumAdapter<Engine::MaterialUsage>::ToString(ToMaterialUsage(type)) },
			{ "passes", nlohmann::json::array({ pass }) },
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
		if (ImGui::BeginTabItem("パラメータ編集")) {

			DrawMaterialParameterSection(context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Particle PS編集")) {

			DrawParticleShaderSection(context);
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

	// 対象シェーダー、ParticleはPSだけを選び形状ステージをRendererに任せる
	ImGui::Separator();
	{
		AssetEditSetting setting{};
		if (createType_ != MaterialCreateType::Particle) {
			MyGUI::AssetReferenceField("VertexShader 必須", createVS_, assetDatabase, { AssetType::Shader }, setting);
		}
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

	const bool needsVS = createType_ != MaterialCreateType::Particle;
	const bool canCreate = (!needsVS || static_cast<bool>(createVS_)) &&
		static_cast<bool>(createPS_) && !createRelativePath_.empty();
	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		CreateMaterialAssets(context);
	}
	ImGui::EndDisabled();

	if (!createMessage_.empty()) {
		ImGui::TextWrapped("%s", createMessage_.c_str());
	}
}

void Engine::MaterialEditorTool::DrawParticleShaderSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	AssetEditSetting setting{};
	AssetID material = editParticleMaterial_;
	if (MyGUI::AssetReferenceField("Particle Material", material,
		assetDatabase, { AssetType::Material }, setting).valueChanged) {

		editParticleMaterial_ = material;
		LoadParticleShaderSource(context);
	}
	if (!editShaderSourcePath_.empty()) {
		ImGui::TextDisabled("%s", editShaderSourcePath_.string().c_str());
	}
	TextEditSetting sourceSetting{};
	sourceSetting.multiLine = true;
	sourceSetting.size = ImVec2(0.0f, (std::max)(240.0f, ImGui::GetContentRegionAvail().y - 70.0f));
	sourceSetting.flags = ImGuiInputTextFlags_AllowTabInput;
	MyGUI::InputText("##ParticlePixelShaderSource", editShaderSource_, sourceSetting);

	const bool canSave = !editShaderSourcePath_.empty() && editShaderSource_ != editShaderOriginal_;
	ImGui::BeginDisabled(!canSave);
	if (ImGui::Button("保存して反映", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0.0f))) {
		SaveParticleShaderSource(context);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(editShaderSourcePath_.empty());
	if (ImGui::Button("再読込", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		LoadParticleShaderSource(context);
	}
	ImGui::EndDisabled();
	if (!editShaderMessage_.empty()) {
		ImGui::TextWrapped("%s", editShaderMessage_.c_str());
	}
}

void Engine::MaterialEditorTool::DrawMaterialParameterSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	AssetEditSetting setting{};
	AssetID material = editParameterMaterial_;
	if (MyGUI::AssetReferenceField("Material", material,
		assetDatabase, { AssetType::Material }, setting).valueChanged) {

		editParameterMaterial_ = material;
		LoadMaterialParameters(context, material);
	}
	if (!editParameterDraftValid_) {

		if (!editParameterMessage_.empty()) {
			ImGui::TextWrapped("%s", editParameterMessage_.c_str());
		}
		return;
	}

	if (MyGUI::EnumCombo("用途", editParameterDraft_.usage).valueChanged) {
		editParameterDirty_ = true;
	}

	const ShaderReflectionInfo* reflection = nullptr;
	if (context.panelContext && context.panelContext->renderPipeline) {
		reflection = context.panelContext->renderPipeline->FindMaterialDrawReflection(editParameterDraft_);
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Parameters", true)) {

		if (!reflection) {
			ImGui::TextDisabled("シェーダーreflectionをまだ取得できません");
			ImGui::TextDisabled("対象マテリアルが一度描画されると自動で列挙されます");
		} else {
			const size_t parameterCount = editParameterDraft_.parameters.size();
			editParameterDirty_ |= MaterialParameterEditor::DrawReflectedCBufferParameters(
				*reflection, MaterialParameterCBuffer::kSurface, editParameterDraft_.parameters);
			editParameterDirty_ |= parameterCount != editParameterDraft_.parameters.size();
		}
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Textures", true)) {

		bool anyTexture = false;
		std::unordered_set<std::string> drawnTextures;
		if (reflection) {
			for (const ShaderResourceBinding& resource : reflection->resources) {

				if (resource.kind != ShaderBindingKind::SRV || resource.space != 2 ||
					resource.rawType != D3D_SIT_TEXTURE || drawnTextures.count(resource.name) != 0) {
					continue;
				}
				drawnTextures.insert(resource.name);
				anyTexture = true;

				AssetID texture{};
				const auto it = editParameterDraft_.parameters.find(resource.name);
				if (it != editParameterDraft_.parameters.end()) {
					if (const AssetID* id = std::get_if<AssetID>(&it->second.value)) {
						texture = *id;
					}
				}
				if (MyGUI::AssetReferenceField(resource.name.c_str(), texture,
					assetDatabase, { AssetType::Texture }, setting).editFinished) {

					editParameterDraft_.parameters[resource.name].value = texture;
					editParameterDirty_ = true;
				}
			}
		}
		if (!anyTexture) {
			ImGui::TextDisabled("space2のマテリアルテクスチャがありません");
		}
	}

	ImGui::Spacing();
	ImGui::BeginDisabled(!editParameterDirty_);
	if (ImGui::Button("保存して反映", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		SaveMaterialParameters(context);
	}
	ImGui::EndDisabled();
	if (!editParameterMessage_.empty()) {
		ImGui::TextWrapped("%s", editParameterMessage_.c_str());
	}
}

bool Engine::MaterialEditorTool::LoadMaterialParameters(const EditorToolContext& context, AssetID materialID) {

	editParameterDraft_ = MaterialAsset{};
	editParameterDraftValid_ = false;
	editParameterDirty_ = false;
	editParameterMessage_.clear();
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || !materialID) {
		return false;
	}

	const std::filesystem::path path = assetDatabase->ResolveFullPath(materialID);
	if (path.empty() || !FromJson(JsonAdapter::Load(path.string(), false), editParameterDraft_)) {
		editParameterMessage_ = "マテリアルを読み込めません";
		return false;
	}
	editParameterDraft_.guid = materialID;
	editParameterDraftValid_ = true;
	editParameterMessage_ = "読み込みました";
	return true;
}

bool Engine::MaterialEditorTool::SaveMaterialParameters(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || !editParameterDraftValid_ || !editParameterMaterial_) {
		editParameterMessage_ = "保存対象のマテリアルがありません";
		return false;
	}

	const std::filesystem::path path = assetDatabase->ResolveFullPath(editParameterMaterial_);
	if (path.empty()) {
		editParameterMessage_ = "マテリアルの保存先を解決できません";
		return false;
	}
	editParameterDraft_.guid = editParameterMaterial_;
	JsonAdapter::Save(path.string(), ToJson(editParameterDraft_));
	if (context.panelContext && context.panelContext->renderPipeline) {
		context.panelContext->renderPipeline->ReloadMaterial(editParameterMaterial_);
	}
	editParameterDirty_ = false;
	editParameterMessage_ = "保存して描画へ反映しました";
	return true;
}

bool Engine::MaterialEditorTool::LoadParticleShaderSource(const EditorToolContext& context) {

	editShaderAsset_ = {};
	editShaderSourcePath_.clear();
	editShaderSource_.clear();
	editShaderOriginal_.clear();
	editShaderMessage_.clear();
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || !editParticleMaterial_) {
		return false;
	}

	const std::filesystem::path materialPath = assetDatabase->ResolveFullPath(editParticleMaterial_);
	MaterialAsset material{};
	if (materialPath.empty() || !FromJson(JsonAdapter::Load(materialPath.string(), false), material)) {
		editShaderMessage_ = "マテリアルを読み込めません";
		return false;
	}
	const MaterialPassBinding* pass = FindPass(material, MaterialPassKind::Transparent);
	if (!pass) {
		pass = FindPass(material, MaterialPassKind::Draw);
	}
	if (!pass || !pass->shaderOverride) {
		editShaderMessage_ = "shaderOverrideを持つParticleマテリアルではありません";
		return false;
	}

	editShaderAsset_ = pass->shaderOverride;
	const std::filesystem::path shaderPath = assetDatabase->ResolveFullPath(editShaderAsset_);
	ShaderAsset shader{};
	if (shaderPath.empty() || !FromJson(JsonAdapter::Load(shaderPath.string(), false), shader)) {
		editShaderMessage_ = "部分シェーダーを読み込めません";
		return false;
	}
	const ShaderStageEntry* pixelStage = FindShaderStage(shader, ShaderStage::PS);
	if (!pixelStage) {
		editShaderMessage_ = "Pixel Shaderステージがありません";
		return false;
	}
	const std::optional<AssetID> sourceID = TryParseUUID16Hex(pixelStage->file);
	if (!sourceID) {
		editShaderMessage_ = "Pixel Shaderのソース参照がGUIDではありません";
		return false;
	}
	editShaderSourcePath_ = assetDatabase->ResolveFullPath(*sourceID);
	std::ifstream ifs(editShaderSourcePath_, std::ios::binary);
	if (!ifs.is_open()) {
		editShaderMessage_ = "Pixel Shaderソースを開けません";
		editShaderSourcePath_.clear();
		return false;
	}
	editShaderSource_.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	editShaderOriginal_ = editShaderSource_;
	editShaderEntry_ = pixelStage->entry.empty() ? "main" : pixelStage->entry;
	editShaderProfile_ = pixelStage->profile.empty() ? "ps_6_6" : pixelStage->profile;
	editShaderMessage_ = "読み込みました";
	return true;
}

bool Engine::MaterialEditorTool::SaveParticleShaderSource(const EditorToolContext& context) {

	if (editShaderSourcePath_.empty() || !context.panelContext ||
		!context.panelContext->graphicsPlatform || !context.panelContext->renderPipeline) {
		editShaderMessage_ = "シェーダーコンパイラを利用できません";
		return false;
	}
	auto writeSource = [&](const std::string& source) {
		std::ofstream ofs(editShaderSourcePath_, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) {
			return false;
		}
		ofs.write(source.data(), static_cast<std::streamsize>(source.size()));
		return ofs.good();
		};
	if (!writeSource(editShaderSource_)) {
		editShaderMessage_ = "Pixel Shaderソースを保存できません";
		return false;
	}

	const std::wstring profile(editShaderProfile_.begin(), editShaderProfile_.end());
	const std::wstring entry(editShaderEntry_.begin(), editShaderEntry_.end());
	DxShaderCompiler* compiler = context.panelContext->graphicsPlatform->GetDxShaderCompiler();
	const CompiledShader compiled = compiler->CompileShader(editShaderSourcePath_.wstring(),
		profile.c_str(), entry.c_str(), ShaderStage::PS);
	if (!compiled.object) {

		writeSource(editShaderOriginal_);
		editShaderMessage_ = "コンパイルに失敗したため保存内容を元に戻しました。詳細はConsoleを確認してください";
		return false;
	}

	editShaderOriginal_ = editShaderSource_;
	context.panelContext->renderPipeline->ReloadShader(editShaderAsset_);
	editShaderMessage_ = "コンパイル成功。描画へ反映しました";
	return true;
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
	} else if (type == MaterialCreateType::Particle) {

		// Particleは半透明描画を前提に深度テストのみ有効にする
		settings.cullMode = D3D12_CULL_MODE_NONE;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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

	const MaterialUsage usage = EnumAdapter<MaterialUsage>::FromString(
		materialData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
	if (const std::optional<MaterialCreateType> type = ToMaterialCreateType(usage)) {
		createType_ = *type;
		ApplyTypeDefaults(createType_);
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

	// DrawかTransparentパスを優先して参照pipelineと部分シェーダーを引く
	AssetID pipelineID{};
	AssetID shaderOverrideID{};
	for (const auto& pass : materialData["passes"]) {
		const std::string passKind = pass.value("passKind", std::string{});
		if (passKind == "Draw" || passKind == "Transparent") {

			pipelineID = ParseAssetID(pass, "pipeline");
			shaderOverrideID = ParseAssetID(pass, "shaderOverride");
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

	// 部分シェーダーがあれば優先し、無ければ先頭variantのshaderを引く
	const AssetID shaderID = shaderOverrideID ?
		shaderOverrideID : ParseAssetID(pipelineData["variants"].front(), "shader");
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
	if ((createType_ != MaterialCreateType::Particle && !createVS_) || !createPS_) {
		createMessage_ = createType_ == MaterialCreateType::Particle ?
			"PixelShaderは必須です" : "VertexShaderとPixelShaderは必須です";
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
	const bool isParticle = createType_ == MaterialCreateType::Particle;
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
		MakeShaderJson(baseName, createVS_, createPS_, createMS_, createAS_, createGS_,
			useMeshShader, useGeometryShader, isParticle));
	const AssetID shaderID = assetDatabase->ImportOrGet(shaderLogical, AssetType::Shader);
	if (!shaderID) {
		createMessage_ = "shader.jsonの登録に失敗しました";
		return false;
	}

	// pipelineを書き出して登録し、得たGUIDをmaterialが参照する
	const AssetID pipelineShader = isParticle ? BuiltinAssets::Shaders::Particle : shaderID;
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(pipelineLogical).string(),
		MakePipelineJson(baseName, pipelineShader, useMeshShader, useGeometryShader, numRenderTargets, createPipeline_));
	const AssetID pipelineID = assetDatabase->ImportOrGet(pipelineLogical, AssetType::RenderPipeline);
	if (!pipelineID) {
		createMessage_ = "pipeline.jsonの登録に失敗しました";
		return false;
	}

	// materialを書き出して登録する、同用途の取り込み元があればパラメータも引き継ぐ
	nlohmann::json materialData = MakeMaterialJson(baseName, pipelineID, isParticle ? shaderID : AssetID{},
		createType_, useMeshShader, useGeometryShader);
	if (createSourceMaterial_) {

		const std::filesystem::path sourcePath = assetDatabase->ResolveFullPath(createSourceMaterial_);
		const nlohmann::json sourceData = sourcePath.empty() ?
			nlohmann::json{} : JsonAdapter::Load(sourcePath.string(), false);
		const MaterialUsage sourceUsage = EnumAdapter<MaterialUsage>::FromString(
			sourceData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
		if (sourceUsage == ToMaterialUsage(createType_) && sourceData.contains("parameters") &&
			sourceData["parameters"].is_object()) {

			materialData["parameters"] = sourceData["parameters"];
		}
	}
	JsonAdapter::Save(assetDatabase->ResolveAssetPath(materialLogical).string(), materialData);
	const AssetID materialID = assetDatabase->ImportOrGet(materialLogical, AssetType::Material);
	if (!materialID) {
		createMessage_ = "material.jsonの登録に失敗しました";
		return false;
	}

	editParameterMaterial_ = materialID;
	LoadMaterialParameters(context, materialID);
	Logger::Output(LogType::Engine, "[MaterialEditorTool] created material assets. path={}", materialLogical);
	createMessage_ = "作成しました " + materialLogical;
	return true;
}
