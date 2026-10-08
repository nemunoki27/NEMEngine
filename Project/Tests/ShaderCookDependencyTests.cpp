#include "ShaderCookDependencyTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDependencyResolver.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>

namespace {

	//============================================================================
	//	ProductPathScope class
	//	検証製品のパスだけを一時的に使用する
	//============================================================================
	class ProductPathScope {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ProductPathScope(const std::filesystem::path& root) {

			previousPath_ = std::filesystem::current_path();
			char* portable = nullptr;
			size_t portableLength = 0;
			if (_dupenv_s(&portable, &portableLength, "NEMENGINE_PORTABLE") == 0 && portable) {
				previousPortable_ = portable;
			}
			std::free(portable);
			std::filesystem::copy_file(Engine::RuntimePaths::GetProjectDescriptorPath(), root / "CookProbe.nemproject");
			std::ofstream(root / ".nemBuildManifest.json") << "{}";
			try {
				_putenv_s("NEMENGINE_PORTABLE", "1");
				std::filesystem::current_path(root);
				Engine::RuntimePaths::Refresh();
			} catch (...) {
				Restore();
				throw;
			}
		}
		~ProductPathScope() { Restore(); }
		ProductPathScope(const ProductPathScope&) = delete;
		ProductPathScope& operator=(const ProductPathScope&) = delete;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 元の作業パス
		std::filesystem::path previousPath_;
		// 元のPortable設定
		std::optional<std::string> previousPortable_;

		//--------- functions ----------------------------------------------------

		// 検証前のパスへ戻す
		void Restore() {

			std::filesystem::current_path(previousPath_);
			_putenv_s("NEMENGINE_PORTABLE", previousPortable_ ? previousPortable_->c_str() : "");
			Engine::RuntimePaths::Refresh();
		}
	};
}

bool NEMTests::TestShaderCookDependencies() {

	using namespace Engine;
	TestDirectory directory("ShaderCookDependencies", RuntimePaths::GetSavedPath("Tests"));
	const auto root = directory.GetPath();
	const AssetID shaderID{71, 1}, pipelineID{71, 2}, missingID{71, 3}, materialID{71, 4};
	ShaderAsset shader;
	shader.guid = shaderID;
	shader.name = "CookedDependency";
	ShaderStageEntry stage;
	stage.stage = ShaderStage::PS;
	stage.file = "probe.PS.hlsl";
	stage.profile = "ps_6_0";
	shader.stages.push_back(stage);
	RenderPipelineAsset pipeline;
	pipeline.guid = pipelineID;
	auto pipelineData = ToJson(pipeline);
	pipelineData["guid"] = ToString(pipelineID);
	const auto cookRoot = root / "Cooked/Shaders";
	std::filesystem::create_directories(cookRoot);
	const auto shaderPath = root / "probe.PS.hlsl";
	std::ofstream(shaderPath) << "float4 main() : SV_Target { return float4(1, 0, 0, 1); }";
	DxShaderCompiler compiler;
	compiler.Init();
	const auto compiled = compiler.CompileShader(shaderPath.wstring(), L"ps_6_0", L"main", ShaderStage::PS);
	if (!compiled.IsValid() ||
		!ShaderCookStorage::WriteBinary(cookRoot / "probe.dxil", compiled.bytecode.data(), compiled.bytecode.size())) {
		return false;
	}
	std::filesystem::remove(shaderPath);
	const nlohmann::json manifest{{"schemaVersion", ShaderCookStorage::kShaderCookSchemaVersion}, {"cookedOnly", true},
		{"shaders", nlohmann::json::array({{{"asset", ShaderCookStorage::WriteShaderMetadata(shader)},
						{"stages", nlohmann::json::array(
									   {{{"stage", "PS"}, {"entry", "main"}, {"profile", "ps_6_0"}, {"bytecode", "probe.dxil"},
										   {"reflection", ShaderCookStorage::WriteReflection(compiled.reflection)}}})}}})},
		{"pipelines", nlohmann::json::array({pipelineData})}};
	std::ofstream(cookRoot / "ShaderCookManifest.json") << manifest.dump();
	const auto assets = root / "GameAssets";
	std::filesystem::create_directories(assets);

	// 正しいCook参照と種別違い・欠損を同じ文書で検証する
	const nlohmann::json material{
		{"passes", nlohmann::json::array({{{"pipeline", ToString(pipelineID)}, {"shaderOverride", ToString(shaderID)}},
					   {{"pipeline", ToString(shaderID)}, {"shaderOverride", ToString(missingID)}}})}};
	std::ofstream(assets / "probe.material.json") << material.dump();
	{
		ProductPathScope product(root);
		if (!RuntimePaths::IsProductBuild()) {
			return false;
		}
		AssetType type = AssetType::Texture;
		if (!ShaderCook::TryGetAssetType(shaderID, type) || type != AssetType::Shader ||
			!ShaderCook::TryGetAssetType(pipelineID, type) || type != AssetType::RenderPipeline ||
			ShaderCook::TryGetAssetType(missingID, type) || type != AssetType::RenderPipeline) {
			return false;
		}
		AssetDatabase database;
		database.Init();
		AssetMeta meta;
		meta.guid = materialID;
		meta.type = AssetType::Material;
		meta.assetPath = "GameAssets/probe.material.json";
		std::vector<AssetDatabaseIssue> issues;
		const auto dependencies = AssetDependencyResolver::ExtractDependencies(database, meta, issues);
		if (dependencies.size() != 3 || issues.size() != 2) {
			return false;
		}
		const bool missing = std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
			return issue.type == AssetDatabaseIssueType::MissingReference && issue.referencedAssetID == missingID;
		});
		const bool mismatch = std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
			return issue.type == AssetDatabaseIssueType::ReferenceTypeMismatch && issue.referencedAssetID == shaderID &&
				   issue.expectedType == AssetType::RenderPipeline && issue.actualType == AssetType::Shader;
		});
		if (!missing || !mismatch) {
			return false;
		}
	}
	// Source実行へ戻した後は検証製品のCookを借用しない
	AssetType sourceType = AssetType::Texture;
	return !ShaderCook::TryGetAssetType(shaderID, sourceType) && sourceType == AssetType::Texture;
}
