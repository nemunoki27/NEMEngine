#include "EditMaterialAssetCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <stdexcept>
#include <utility>

//============================================================================
//	EditMaterialAssetCommand classMethods
//============================================================================
Engine::EditMaterialAssetCommand::EditMaterialAssetCommand(AssetID asset, std::filesystem::path project,
	nlohmann::json before, nlohmann::json after)
	: asset_(asset), project_(std::move(project)), before_(std::move(before)), after_(std::move(after)) {}

bool Engine::EditMaterialAssetCommand::Execute(EditorCommandContext& context) {

	return Apply(context, before_, after_);
}

void Engine::EditMaterialAssetCommand::Undo(EditorCommandContext& context) {

	Apply(context, after_, before_);
}

bool Engine::EditMaterialAssetCommand::Apply(EditorCommandContext& context, const nlohmann::json& expected,
	const nlohmann::json& next) const {

	auto* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	const auto* meta = database ? database->Find(asset_) : nullptr;
	if (!meta || database->GetProjectRoot() != project_ || meta->type != AssetType::Material) {
		throw std::runtime_error("編集対象のマテリアルを解決できません");
	}

	AssetDocumentChange change;
	std::string error;
	if (!AssetDocumentPublication::Prepare(*database, meta->assetPath, meta->type, change, error)) {
		throw std::runtime_error(error);
	}
	// 外部で更新された文書に古い履歴を適用しない
	nlohmann::json current;
	if (!JsonAdapter::TryLoad(change.filePath, current) || current != expected) {
		throw std::runtime_error("マテリアルが変更されています 読み込み直してください");
	}
	change.document = next;
	if (!AssetDocumentPublication::Commit(*database, {&change, 1},
		AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Material), error)) {
		throw std::runtime_error(error);
	}

	const auto* systems = context.editorContext->systemContext;
	if (systems && systems->renderAssetLibrary) {
		systems->renderAssetLibrary->InvalidateMaterial(asset_);
	}
	return true;
}
