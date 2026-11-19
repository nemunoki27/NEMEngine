#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Material/MaterialCreationSession.h>
#include <Engine/Editor/Tools/Builtin/Material/MaterialCreationImportUtility.h>
#include <Engine/Editor/Tools/Builtin/Material/MaterialCreationDocument.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/Inspectors/Common/MaterialReflectionCache.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterDefaults.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <algorithm>
#include <iostream>
#include <variant>

bool NEMTests::TestMaterialReflectionCache() {

	using namespace Engine;
	ShaderConstantBufferVariable variable;
	variable.valueType = D3D_SVT_FLOAT;
	// Shaderの型と色指定に合わせた既定値を確認
	for (UINT count = 1; count <= 4; ++count) {

		variable.declaredComponentCount = count;
		auto value = MaterialParameterDefaults::BuildValue(variable);
		if ((count == 1 && !std::holds_alternative<float>(value.value)) ||
			(count == 2 && !std::holds_alternative<Vector2>(value.value)) ||
			(count == 3 && !std::holds_alternative<Vector3>(value.value)) ||
			(count == 4 && !std::holds_alternative<Vector4>(value.value))) {
			return false;
		}
	}
	variable.name = "BaseCOLOR";
	if (!std::holds_alternative<Color4>(MaterialParameterDefaults::BuildValue(variable).value)) {
		return false;
	}
	variable.name = "Tint";
	variable.isColor = true;
	if (!std::holds_alternative<Color4>(MaterialParameterDefaults::BuildValue(variable).value)) {
		return false;
	}
	variable.valueType = D3D_SVT_INT;
	if (!std::holds_alternative<int32_t>(MaterialParameterDefaults::BuildValue(variable).value)) {
		return false;
	}
	variable.valueType = D3D_SVT_UINT;
	if (!std::holds_alternative<uint32_t>(MaterialParameterDefaults::BuildValue(variable).value)) {
		return false;
	}
	variable.valueType = D3D_SVT_BOOL;
	if (!std::holds_alternative<bool>(MaterialParameterDefaults::BuildValue(variable).value)) {
		return false;
	}
	TestDirectory directory("MaterialCache", RuntimePaths::GetGameAssetsRoot() / "Materials");
	auto path = directory.GetPath() / std::filesystem::path(u8"Material確認.material.json");
	auto save = [&](float speed) {

		MaterialAsset material;
		material.name = "CacheFixture";
		MaterialParameterValue value;
		value.value = speed;
		material.parameters.Set("speed", value);
		return JsonFile::Save(path, ToJson(material));
	};
	if (!save(1.0f)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	auto id = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Material);
	if (!id) {
		return false;
	}
	RenderPipelineRunner runner;
	EditorContext editor;
	editor.assetDatabase = &database;
	EditorPanelContext context;
	context.editorContext = &editor;
	context.renderPipeline = &runner;
	MaterialReflectionCache cache;
	auto matches = [&](float expected) {

		// GPUを作らずMaterial文書の更新だけを確認
		if (cache.EnsureReflection(context, {}, id)) {
			return false;
		}
		auto value = cache.GetMaterial().parameters.FindByName("speed");
		return value && std::holds_alternative<float>(value->value) && std::get<float>(value->value) == expected;
	};
	if (!matches(1.0f) || !save(2.0f)) {
		return false;
	}
	// 同じGUIDの保存と索引再構築を反映
	database.NotifyContentChanged(id);
	if (!matches(2.0f) || !save(3.0f) || !database.RebuildMeta({directory.GetPath()}) || !matches(3.0f)) {
		return false;
	}
	AssetDatabase replacement = database;
	if (replacement.GetStructureRevision() != database.GetStructureRevision() ||
		replacement.GetContentRevision(id) != database.GetContentRevision(id) || !save(4.0f)) {
		return false;
	}
	// 同じ更新番号でも別の索引へ切り替わったら再読込
	editor.assetDatabase = &replacement;
	if (!matches(4.0f) || !save(5.0f)) {
		return false;
	}
	replacement = database;
	if (!matches(5.0f) || !JsonFile::Save(path, nlohmann::json(42))) {
		return false;
	}
	// 破損文書を旧値で隠さず、修復後に読み直す
	replacement.NotifyContentChanged(id);
	cache.EnsureReflection(context, {}, id);
	if (cache.GetMaterial().parameters.FindByName("speed") || !save(6.0f)) {
		return false;
	}
	replacement.NotifyContentChanged(id);
	if (!matches(6.0f)) {
		return false;
	}
	// 読込できない間は空文書を成功としてCacheしない
	std::filesystem::remove(path);
	replacement.NotifyContentChanged(id);
	cache.EnsureReflection(context, {}, id);
	if (cache.GetMaterial().parameters.FindByName("speed") || !save(7.0f)) {
		return false;
	}
	// 同じ更新番号でも失敗した読込は再試行する
	return matches(7.0f);
}

bool NEMTests::TestMaterialCreationFailures() {

	using namespace Engine;
	const auto fail = [](int line) {
		std::cerr << "Material creation contract failed at MaterialCreationTests.cpp:" << line << '\n';
		return false;
	};
	TestDirectory directory("MaterialCreation", RuntimePaths::GetGameAssetsRoot() / "Materials");
	const auto shaderPath = directory.GetPath() / "Fixture.shader.json";
	const auto pipelinePath = directory.GetPath() / "Fixture.pipeline.json";
	const nlohmann::json initialShader = {{"name", "retained"}, {"stages", nlohmann::json::array()}};
	if (!JsonFile::Save(shaderPath, initialShader)) {
		return fail(__LINE__);
	}
	AssetDatabase database;
	database.Init();
	const auto shaderID = database.ImportOrGet(RuntimePaths::ToAssetPath(shaderPath), AssetType::Shader);
	if (!shaderID) {
		return fail(__LINE__);
	}
	MaterialCreationSession session;
	session.ApplyTypeDefaults(MaterialCreateType::Sprite);
	session.GetDraft().createType = MaterialCreateType::Sprite;
	session.GetDraft().createVS = AssetID{1, 2};
	session.GetDraft().createPS = AssetID{3, 4};
	session.GetDraft().createRelativePath = RuntimePaths::ToAssetPath(directory.GetPath() / "Fixture");
	EditorToolContext context;
	context.toolContext.assetDatabase = &database;
	const auto revision = database.GetStructureRevision();
	{
		// 保存失敗を既存GUIDの取得成功で隠さない
		TestFileReadLock lock(shaderPath);
		if (session.CreateMaterialAssets(context) || session.GetDraft().createMessage.empty() ||
			std::filesystem::exists(pipelinePath) || database.GetStructureRevision() != revision) {
			return fail(__LINE__);
		}
	}
	nlohmann::json data;
	if (!JsonFile::TryLoad(shaderPath, data) || data != initialShader) {
		return fail(__LINE__);
	}

	// 途中まで解析できても不正な型があれば設定を変更しない
	nlohmann::json pipeline = {{"variants", nlohmann::json::array({{{"rasterizer", {{"cullMode", "D3D12_CULL_MODE_BACK"}}},
												{"depthStencil", {{"depthEnable", "invalid"}}}}})}};
	if (!JsonFile::Save(pipelinePath, pipeline)) {
		return fail(__LINE__);
	}
	const auto pipelineID = database.ImportOrGet(RuntimePaths::ToAssetPath(pipelinePath), AssetType::RenderPipeline);
	PipelineCreateSettings settings;
	settings.cullMode = D3D12_CULL_MODE_NONE;
	settings.depthEnable = false;
	if (!pipelineID || MaterialCreationImportUtility::ReadPipelineSettings(database, pipelineID, settings) ||
		settings.cullMode != D3D12_CULL_MODE_NONE || settings.depthEnable) {
		return fail(__LINE__);
	}
	pipeline["variants"][0]["depthStencil"]["depthEnable"] = true;
	pipeline["variants"][0]["disableNormalBlending"] = true;
	if (!JsonFile::Save(pipelinePath, pipeline) ||
		!MaterialCreationImportUtility::ReadPipelineSettings(database, pipelineID, settings) ||
		settings.cullMode != D3D12_CULL_MODE_BACK || !settings.depthEnable || !settings.disableNormalBlending) {
		return fail(__LINE__);
	}
	const nlohmann::json generatedPipeline = MaterialCreationDocument::MakePipelineJson(
		"BlendStateTest", shaderID, true, false, 3, settings);
	if (generatedPipeline["variants"].size() != 2 ||
		!generatedPipeline["variants"][0].value("disableNormalBlending", false) ||
		!generatedPipeline["variants"][1].value("disableNormalBlending", false)) {
		return fail(__LINE__);
	}
	pipeline["variants"][0].erase("disableNormalBlending");
	if (!JsonFile::Save(pipelinePath, pipeline) ||
		!MaterialCreationImportUtility::ReadPipelineSettings(database, pipelineID, settings) || settings.disableNormalBlending) {
		return fail(__LINE__);
	}

	// 不正なステージでは既存のShader参照を保持する
	const AssetID pixelID{3, 4};
	data["stages"] =
		nlohmann::json::array({{{"stage", "PS"}, {"file", ToAssetReferenceJson(pixelID)}, {"entry", "mainPixel"}}, 42});
	if (!JsonFile::Save(shaderPath, data)) {
		return fail(__LINE__);
	}
	const nlohmann::json pass = {{"shaderOverride", ToAssetReferenceJson(shaderID)}};
	MaterialCreationImportUtility::MaterialShaderReferences references;
	references.ps = AssetID{8, 9};
	if (MaterialCreationImportUtility::ReadShaderReferences(database, pass, references) || references.ps != AssetID{8, 9}) {
		return fail(__LINE__);
	}
	data["stages"].erase(1);
	if (!JsonFile::Save(shaderPath, data) || !MaterialCreationImportUtility::ReadShaderReferences(database, pass, references) ||
		references.ps != pixelID || references.psEntry != "mainPixel") {
		return fail(__LINE__);
	}

	// 後段の保存失敗では先に書いた文書と索引も戻す
	const auto targetMaterialPath = directory.GetPath() / "Fixture.material.json";
	if (!JsonFile::Save(targetMaterialPath, {{"name", "retained"}, {"passes", nlohmann::json::array()}})) {
		return fail(__LINE__);
	}
	const auto targetMaterialID = database.ImportOrGet(RuntimePaths::ToAssetPath(targetMaterialPath), AssetType::Material);
	if (!targetMaterialID) {
		return fail(__LINE__);
	}
	const JsonFileJournal::Scope scope{directory.GetPath() / "Recovery",
		[&](const std::filesystem::path& path) { return StorageFileUtility::IsInside(path, directory.GetPath()); }};
	std::string diagnostic;
	std::vector<AssetDocumentChange> changes;
	const auto prepareUpdate = [&](const std::filesystem::path& path, AssetType type) {
		AssetDocumentChange change;
		if (!AssetDocumentPublication::Prepare(database, RuntimePaths::ToAssetPath(path), type, change, diagnostic) ||
			!JsonFile::TryLoad(path, change.document)) return false;
		change.document["name"] = "UpdatedFixture";
		changes.emplace_back(std::move(change));
		return true;
	};
	if (!prepareUpdate(shaderPath, AssetType::Shader) || !prepareUpdate(pipelinePath, AssetType::RenderPipeline) ||
		!prepareUpdate(targetMaterialPath, AssetType::Material)) {
		return fail(__LINE__);
	}
	const auto savedShader = StorageFileUtility::FileRevision(shaderPath);
	const auto savedPipeline = StorageFileUtility::FileRevision(pipelinePath);
	const auto savedMaterial = StorageFileUtility::FileRevision(targetMaterialPath);
	const auto savedStructure = database.GetStructureRevision();
	const auto savedContent = database.GetContentRevision();
	const auto savedLifetime = database.GetCacheLifetime();
	for (const auto& lockedPath : {pipelinePath, targetMaterialPath}) {
		{
			TestFileReadLock lock(lockedPath);
			if (AssetDocumentPublication::Commit(database, changes, scope, diagnostic) || diagnostic.empty()) {
				return fail(__LINE__);
			}
		}
		if (StorageFileUtility::FileRevision(shaderPath) != savedShader ||
			StorageFileUtility::FileRevision(pipelinePath) != savedPipeline ||
			StorageFileUtility::FileRevision(targetMaterialPath) != savedMaterial ||
			database.GetStructureRevision() != savedStructure || database.GetContentRevision() != savedContent ||
			savedLifetime.expired() || database.GetCacheLifetime().lock() != savedLifetime.lock()) {
			return fail(__LINE__);
		}
	}

	// 成功した場合だけ新しい文書と索引を公開する
	if (!AssetDocumentPublication::Commit(database, changes, scope, diagnostic) || !savedLifetime.expired() ||
		database.FindByPath(RuntimePaths::ToAssetPath(shaderPath))->guid != shaderID ||
		database.FindByPath(RuntimePaths::ToAssetPath(pipelinePath))->guid != pipelineID ||
		database.FindByPath(RuntimePaths::ToAssetPath(targetMaterialPath))->guid != targetMaterialID ||
		database.GetContentRevision() <= savedContent) {
		return fail(__LINE__);
	}

	// 新規作成は既存Materialを変更せず、新しい名前でだけ作成する
	const auto existingContent = database.GetContentRevision();
	if (session.CreateMaterialAssets(context) || session.GetDraft().createMessage.empty() ||
		database.GetContentRevision() != existingContent) return fail(__LINE__);
	session.GetDraft().createRelativePath = RuntimePaths::ToAssetPath(directory.GetPath() / "NewFixture");
	if (!session.CreateMaterialAssets(context) ||
		!database.FindByPath(RuntimePaths::ToAssetPath(directory.GetPath() / "NewFixture.shader.json")) ||
		!database.FindByPath(RuntimePaths::ToAssetPath(directory.GetPath() / "NewFixture.pipeline.json")) ||
		!database.FindByPath(RuntimePaths::ToAssetPath(directory.GetPath() / "NewFixture.material.json"))) {
		return fail(__LINE__);
	}

	// 保存後の参照解析に失敗しても文書と索引を戻す
	changes.resize(1);
	if (!AssetDocumentPublication::Prepare(
			database, RuntimePaths::ToAssetPath(targetMaterialPath), AssetType::Material, changes.front(), diagnostic)) {
		return fail(__LINE__);
	}
	const auto publishedMaterial = StorageFileUtility::FileRevision(targetMaterialPath);
	const auto publishedContent = database.GetContentRevision();
	const auto publishedLifetime = database.GetCacheLifetime();
	changes.front().document = 42;
	if (AssetDocumentPublication::Commit(database, changes, scope, diagnostic) || diagnostic.empty() ||
		StorageFileUtility::FileRevision(targetMaterialPath) != publishedMaterial ||
		database.GetContentRevision() != publishedContent || publishedLifetime.expired() ||
		!JsonFileJournal::GetRecoveries(scope, true).empty()) {
		return fail(__LINE__);
	}

	// 準備後の外部変更は上書きせず保持する
	const nlohmann::json externalMaterial = {{"name", "external"}, {"passes", nlohmann::json::array()}};
	if (!JsonFile::Save(targetMaterialPath, externalMaterial)) {
		return fail(__LINE__);
	}
	const auto externalRevision = StorageFileUtility::FileRevision(targetMaterialPath);
	if (AssetDocumentPublication::Commit(database, changes, scope, diagnostic) || diagnostic.empty() ||
		StorageFileUtility::FileRevision(targetMaterialPath) != externalRevision ||
		database.GetContentRevision() != publishedContent) {
		return fail(__LINE__);
	}

	// 内容が同じでも未登録のAssetを索引へ公開する
	AssetDatabase unregistered;
	unregistered.Init();
	if (!AssetDocumentPublication::Prepare(
			unregistered, RuntimePaths::ToAssetPath(targetMaterialPath), AssetType::Material, changes.front(), diagnostic)) {
		return fail(__LINE__);
	}
	changes.front().document = externalMaterial;
	if (!AssetDocumentPublication::Commit(unregistered, changes, scope, diagnostic) || !unregistered.Find(targetMaterialID) ||
		StorageFileUtility::FileRevision(targetMaterialPath) != externalRevision) {
		return fail(__LINE__);
	}

	// 取込元の検証用に元のPipeline設定を戻す
	if (!JsonFile::Save(pipelinePath, pipeline)) {
		return fail(__LINE__);
	}

	// Materialの取込途中で失敗しても描画タイプを戻す
	const auto sourcePath = directory.GetPath() / "Source.material.json";
	nlohmann::json sourceMaterial = {{"usage", "Mesh"}, {"shaderGraph", ToAssetReferenceJson(AssetID{5, 6})},
		{"passes", nlohmann::json::array({{{"passKind", "Draw"}, {"pipeline", ToAssetReferenceJson(pipelineID)}}, 12})}};
	if (!JsonFile::Save(sourcePath, sourceMaterial)) {
		return fail(__LINE__);
	}
	const auto materialID = database.ImportOrGet(RuntimePaths::ToAssetPath(sourcePath), AssetType::Material);
	if (!materialID) {
		return fail(__LINE__);
	}

	// 不正な取込元では出力と索引を変更しない
	session.GetDraft().createSourceMaterial = materialID;
	session.GetDraft().createRelativePath = RuntimePaths::ToAssetPath(directory.GetPath() / "InvalidSource");
	sourceMaterial["usage"] = 42;
	const auto sourceRevision = database.GetStructureRevision();
	if (!JsonFile::Save(sourcePath, sourceMaterial) || session.CreateMaterialAssets(context) ||
		session.GetDraft().createMessage.empty() || database.GetStructureRevision() != sourceRevision ||
		std::filesystem::exists(directory.GetPath() / "InvalidSource.shader.json") ||
		std::filesystem::exists(directory.GetPath() / "InvalidSource.pipeline.json") ||
		std::filesystem::exists(directory.GetPath() / "InvalidSource.material.json")) {
		return fail(__LINE__);
	}
	sourceMaterial["usage"] = "Mesh";
	if (!JsonFile::Save(sourcePath, sourceMaterial)) {
		return fail(__LINE__);
	}
	session.LoadPipelineSettingsFromMaterial(database, materialID);
	if (session.GetDraft().createType != MaterialCreateType::Sprite || session.GetDraft().createSourceUsesShaderGraph ||
		session.GetDraft().createPipeline.cullMode != D3D12_CULL_MODE_NONE || session.GetDraft().createMessage.empty()) {
		return fail(__LINE__);
	}
	sourceMaterial["passes"].erase(1);
	if (!JsonFile::Save(sourcePath, sourceMaterial)) {
		return fail(__LINE__);
	}
	session.LoadPipelineSettingsFromMaterial(database, materialID);
	return session.GetDraft().createType == MaterialCreateType::Mesh && session.GetDraft().createSourceUsesShaderGraph &&
		   session.GetDraft().createPipeline.cullMode == D3D12_CULL_MODE_BACK;
}

bool NEMTests::TestAssetDocumentRecovery() {

	using namespace Engine;
	TestDirectory assets("AssetRecovery", RuntimePaths::GetGameAssetsRoot() / "Materials");
	const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Material);
	TestDirectory recovery("AssetRecovery", scope.recoveryRoot);
	const auto target = assets.GetPath() / "recovered.material.json";
	const auto backup = recovery.GetPath() / "0.before";
	const nlohmann::json original{{"name", "Before"}};
	const nlohmann::json changed{{"name", "After"}};
	if (!JsonFile::Save(target, original) || !JsonFile::Save(backup, original)) {
		return false;
	}
	const std::string before = StorageFileUtility::FileRevision(target);
	if (!JsonFile::Save(target, changed)) {
		return false;
	}
	const std::string after = StorageFileUtility::FileRevision(target);
	nlohmann::json journal{{"state", "pending"},
		{"files", nlohmann::json::array({{{"path", Algorithm::PathToUTF8(std::filesystem::absolute(target))},
					  {"backup", "0.before"}, {"before", before}, {"after", after}}})}};
	const auto record = recovery.GetPath() / "operation.json";
	if (!JsonFile::Save(record, journal)) {
		return false;
	}
	std::vector<AssetDatabaseIssue> issues;
	// 起動前の復旧で文書を保存前へ戻す
	if (!AssetDocumentRecovery::RecoverPending(issues) || !issues.empty() || JsonFile::Load(target, false) != original) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	const AssetID asset = database.ImportOrGet(RuntimePaths::ToAssetPath(target), AssetType::Material);
	const AssetMeta* retained = database.Find(asset);
	const auto revision = database.GetStructureRevision();
	if (!asset || !retained) {
		return false;
	}
	const nlohmann::json external{{"name", "External"}};
	if (!JsonFile::Save(record, journal) || !JsonFile::Save(target, external)) {
		return false;
	}
	// 外部変更は復旧と保存準備の両方で保護する
	if (AssetDocumentRecovery::RecoverPending(issues) || issues.size() != 1 ||
		issues.front().type != AssetDatabaseIssueType::UnfinishedAssetSave || JsonFile::Load(target, false) != external) {
		return false;
	}
	// 復旧失敗時は索引の参照と世代を維持
	for (bool fullScan : {false, true}) {

		const bool rebuilt = fullScan ? database.RebuildMeta() : database.RebuildMeta({assets.GetPath()});
		if (rebuilt || database.Find(asset) != retained || database.GetStructureRevision() != revision ||
			database.GetLastRebuildError().empty() ||
			std::count_if(database.GetIssues().begin(), database.GetIssues().end(),
				[](const auto& issue) { return issue.type == AssetDatabaseIssueType::UnfinishedAssetSave; }) != 1) {
			return false;
		}
	}
	AssetDocumentChange change;
	std::string diagnostic;
	if (AssetDocumentPublication::Prepare(
			database, RuntimePaths::ToAssetPath(target), AssetType::Material, change, diagnostic) ||
		diagnostic.empty()) {
		return false;
	}
	// 明示確定でだけ競合を解消し保存を再開
	if (!AssetDocumentRecovery::KeepCurrent(recovery.GetPath(), diagnostic) || JsonFile::Load(target, false) != external ||
		!database.RebuildMeta({assets.GetPath()}) || !database.GetLastRebuildError().empty() ||
		std::any_of(database.GetIssues().begin(), database.GetIssues().end(),
			[](const auto& issue) { return issue.type == AssetDatabaseIssueType::UnfinishedAssetSave; }) ||
		!AssetDocumentPublication::Prepare(
			database, RuntimePaths::ToAssetPath(target), AssetType::Material, change, diagnostic)) {
		return false;
	}
	issues.clear();
	return AssetDocumentRecovery::RecoverPending(issues) && issues.empty() && JsonFile::Load(target, false) == external;
}
