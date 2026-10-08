#include "MaterialCreationSession.h"
#include "MaterialCreationImportUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

// json
#include <json.hpp>

//============================================================================
//	MaterialCreationSession classMethods
//============================================================================

using namespace Engine::MaterialCreationImportUtility;

namespace {

	//============================================================================
	//	MaterialCreationDraftUpdate class
	//	設定の取込失敗時に元の入力を戻す
	//============================================================================
	class MaterialCreationDraftUpdate {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit MaterialCreationDraftUpdate(Engine::MaterialCreationDraft& draft) : draft_(draft), original_(draft) {}
		MaterialCreationDraftUpdate(const MaterialCreationDraftUpdate&) = delete;
		MaterialCreationDraftUpdate& operator=(const MaterialCreationDraftUpdate&) = delete;
		~MaterialCreationDraftUpdate() {

			if (!committed_) {
				// 診断を残して設定だけを戻す
				std::string message = std::move(draft_.createMessage);
				draft_ = std::move(original_);
				draft_.createMessage = std::move(message);
			}
		}
		// 取り込んだ設定を確定する
		void Commit() { committed_ = true; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 更新対象と取込前の設定
		Engine::MaterialCreationDraft& draft_;
		Engine::MaterialCreationDraft original_;
		// 取込完了
		bool committed_ = false;
	};

}

void Engine::MaterialCreationSession::ApplyTypeDefaults(MaterialCreateType type) {

	PipelineCreateSettings settings{};
	if (type == MaterialCreateType::Mesh) {

		// Meshは深度を書き込み、裏面を除く
		settings.cullMode = D3D12_CULL_MODE_BACK;
		settings.depthEnable = true;
		settings.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		settings.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		settings.samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		settings.samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	} else if (type == MaterialCreateType::Particle) {

		// Particleは深度を参照して半透明描画する
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

		// Lineは両面を描画して深度を書き込む
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

	// Mesh以外ではMSとASを外す
	if (type != MaterialCreateType::Mesh) {

		draft_.createMS = AssetID{};
		draft_.createAS = AssetID{};
		draft_.createTransparentPS = AssetID{};
	}
	// Line以外ではGSを外す
	if (type != MaterialCreateType::Line) {

		draft_.createGS = AssetID{};
	}
}

void Engine::MaterialCreationSession::LoadPipelineSettingsFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	MaterialCreationDraftUpdate update(draft_);
	try {

		const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
		if (materialPath.empty()) {
			draft_.createMessage = "マテリアルファイルが見つかりません";
			return;
		}

		const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
		draft_.createSourceUsesShaderGraph = static_cast<bool>(ParseAssetID(materialData, "shaderGraph"));
		if (!materialData.is_object() || !materialData.contains("passes") || !materialData["passes"].is_array() ||
			materialData["passes"].empty()) {
			draft_.createMessage = "マテリアルにパスがありません";
			return;
		}

		const MaterialUsage usage =
			EnumAdapter<MaterialUsage>::FromString(materialData.value("usage", "Generic")).value_or(MaterialUsage::Generic);
		if (const std::optional<MaterialCreateType> type = ToMaterialCreateType(usage)) {
			draft_.createType = *type;
			ApplyTypeDefaults(draft_.createType);
		}

		// 通常描画と半透明描画の設定を取り込む
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
			const RenderPhase legacyPhase = RenderPhaseFromString(renderState->value("phase", "Opaque"), RenderPhase::Opaque);
			draft_.createPipeline.surfaceMode = EnumAdapter<MaterialSurfaceMode>::FromString(
				renderState->value("surfaceMode", legacyPhase == RenderPhase::Transparent ? "Transparent" : "Opaque"))
													.value_or(MaterialSurfaceMode::Opaque);
			draft_.createPipeline.blendMode =
				EnumAdapter<BlendMode>::FromString(renderState->value("blendMode", "Normal")).value_or(BlendMode::Normal);
		}
		if (!drawPipelineID) {
			drawPipelineID = ParseAssetID(materialData["passes"].front(), "pipeline");
		}
		if (!drawPipelineID || !ReadPipelineSettings(assetDatabase, drawPipelineID, draft_.createPipeline)) {
			draft_.createMessage = "マテリアルからpipeline参照を取得できません";
			return;
		}
		draft_.createTransparentPass =
			draft_.createType == MaterialCreateType::Mesh && static_cast<bool>(transparentPipelineID);
		if (draft_.createTransparentPass &&
			!ReadPipelineSettings(assetDatabase, transparentPipelineID, draft_.createTransparentPipeline)) {
			draft_.createMessage = "半透明pipelineにvariantがありません";
			return;
		}

		draft_.createMessage = "パイプライン設定を取り込みました";

		update.Commit();
	} catch (const nlohmann::json::exception&) {
		draft_.createMessage = "Materialの設定形式が不正です";
	}
}

void Engine::MaterialCreationSession::LoadShadersFromMaterial(AssetDatabase& assetDatabase, AssetID materialID) {

	MaterialCreationDraftUpdate update(draft_);
	try {

		const std::filesystem::path materialPath = assetDatabase.ResolveFullPath(materialID);
		if (materialPath.empty()) {
			return;
		}
		const nlohmann::json materialData = JsonAdapter::Load(materialPath.string(), false);
		if (!materialData.is_object() || !materialData.contains("passes") || !materialData["passes"].is_array() ||
			materialData["passes"].empty()) {
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
				if (!ReadShaderReferences(assetDatabase, *transparentPass, transparentReferences)) {
					draft_.createMessage = "半透明Shaderを読み込めません";
					return;
				}
				draft_.createTransparentPS = transparentReferences.ps;
				draft_.createTransparentPSEntry = transparentReferences.psEntry;
			}
		}

		draft_.createMessage = "シェーダーを取り込みました";

		update.Commit();
	} catch (const nlohmann::json::exception&) {
		draft_.createMessage = "Materialの設定形式が不正です";
	}
}
