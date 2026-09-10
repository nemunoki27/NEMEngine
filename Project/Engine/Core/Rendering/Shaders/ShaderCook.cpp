#include "ShaderCook.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {

	constexpr uint32_t kShaderCookSchemaVersion = 1;
	constexpr const char* kShaderCookManifest =
		"Cooked/Shaders/ShaderCookManifest.json";

	struct CookedStageRecord {

		Engine::ShaderCookRequest request{};
		std::filesystem::path bytecodePath;
		Engine::ShaderReflectionInfo reflection{};
	};

	struct CookedShaderRecord {

		Engine::ShaderAsset asset{};
		std::vector<CookedStageRecord> stages;
	};

	struct ShaderCookRuntimeState {

		std::filesystem::path manifestPath;
		std::unordered_map<Engine::AssetID, CookedShaderRecord> shaders;
		std::unordered_map<Engine::AssetID, Engine::RenderPipelineAsset> pipelines;
		bool loaded = false;
		bool valid = false;
	};

	ShaderCookRuntimeState g_runtimeState{};
	std::mutex g_runtimeMutex;

	std::string ResolveDefaultProfile(Engine::ShaderStage stage) {

		switch (stage) {
		case Engine::ShaderStage::VS: return "vs_6_6";
		case Engine::ShaderStage::AS: return "as_6_6";
		case Engine::ShaderStage::MS: return "ms_6_6";
		case Engine::ShaderStage::GS: return "gs_6_6";
		case Engine::ShaderStage::PS: return "ps_6_6";
		case Engine::ShaderStage::CS: return "cs_6_6";
		case Engine::ShaderStage::Lib: return "lib_6_6";
		default: return {};
		}
	}

	std::filesystem::path NormalizePath(
		const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	std::wstring MakePathKey(const std::filesystem::path& path) {

		std::wstring key = NormalizePath(path).generic_wstring();
		std::transform(key.begin(), key.end(), key.begin(),
			[](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
		return key;
	}

	nlohmann::json WriteVariable(
		const Engine::ShaderConstantBufferVariable& variable) {

		return {
			{ "name", variable.name },
			{ "parameterID", variable.parameterID.value },
			{ "semantic", static_cast<uint32_t>(variable.semantic) },
			{ "offset", variable.offset },
			{ "size", variable.size },
			{ "valueClass", static_cast<uint32_t>(variable.valueClass) },
			{ "valueType", static_cast<uint32_t>(variable.valueType) },
			{ "rows", variable.rows },
			{ "columns", variable.columns },
			{ "elements", variable.elements },
			{ "declaredComponentCount", variable.declaredComponentCount },
			{ "declaredByteSize", variable.declaredByteSize },
			{ "used", variable.used },
			{ "isColor", variable.isColor },
			{ "isTexture", variable.isTexture },
		};
	}

	bool ReadVariable(const nlohmann::json& data,
		Engine::ShaderConstantBufferVariable& outVariable) {

		if (!data.is_object()) {
			return false;
		}
		outVariable.name = data.value("name", "");
		outVariable.parameterID.value = data.value("parameterID", uint64_t{ 0 });
		outVariable.semantic = static_cast<Engine::MaterialParameterSemantic>(
			data.value("semantic", 0u));
		outVariable.offset = data.value("offset", 0u);
		outVariable.size = data.value("size", 0u);
		outVariable.valueClass = static_cast<D3D_SHADER_VARIABLE_CLASS>(
			data.value("valueClass", 0u));
		outVariable.valueType = static_cast<D3D_SHADER_VARIABLE_TYPE>(
			data.value("valueType", 0u));
		outVariable.rows = data.value("rows", 0u);
		outVariable.columns = data.value("columns", 0u);
		outVariable.elements = data.value("elements", 0u);
		outVariable.declaredComponentCount =
			data.value("declaredComponentCount", 1u);
		outVariable.declaredByteSize = data.value("declaredByteSize", 4u);
		outVariable.used = data.value("used", true);
		outVariable.isColor = data.value("isColor", false);
		outVariable.isTexture = data.value("isTexture", false);
		return !outVariable.name.empty();
	}

	nlohmann::json WriteReflection(
		const Engine::ShaderReflectionInfo& reflection) {

		nlohmann::json data = {
			{ "requiresFlags", reflection.requiresFlags },
			{ "threadGroup", {
				reflection.threadGroupX,
				reflection.threadGroupY,
				reflection.threadGroupZ,
			} },
			{ "resources", nlohmann::json::array() },
			{ "inputs", nlohmann::json::array() },
			{ "constantBuffers", nlohmann::json::array() },
			{ "structuredBuffers", nlohmann::json::array() },
		};
		for (const Engine::ShaderResourceBinding& resource : reflection.resources) {
			data["resources"].push_back({
				{ "name", resource.name },
				{ "parameterID", resource.parameterID.value },
				{ "semantic", static_cast<uint32_t>(resource.semantic) },
				{ "kind", static_cast<uint32_t>(resource.kind) },
				{ "bindPoint", resource.bindPoint },
				{ "bindCount", resource.bindCount },
				{ "space", resource.space },
				{ "stageMask", static_cast<uint32_t>(resource.stageMask) },
				{ "rawType", static_cast<uint32_t>(resource.rawType) },
			});
		}
		for (const Engine::ShaderInputSemantic& input : reflection.inputs) {
			data["inputs"].push_back({
				{ "semanticName", input.semanticName },
				{ "semanticIndex", input.semanticIndex },
				{ "registerIndex", input.registerIndex },
				{ "mask", input.mask },
				{ "componentType", static_cast<uint32_t>(input.componentType) },
			});
		}
		for (const Engine::ShaderConstantBufferInfo& buffer : reflection.constantBuffers) {
			nlohmann::json item = {
				{ "name", buffer.name },
				{ "bindPoint", buffer.bindPoint },
				{ "space", buffer.space },
				{ "size", buffer.size },
				{ "variables", nlohmann::json::array() },
			};
			for (const auto& variable : buffer.variables) {
				item["variables"].push_back(WriteVariable(variable));
			}
			data["constantBuffers"].push_back(std::move(item));
		}
		for (const Engine::ShaderStructuredBufferInfo& buffer : reflection.structuredBuffers) {
			nlohmann::json item = {
				{ "name", buffer.name },
				{ "bindPoint", buffer.bindPoint },
				{ "space", buffer.space },
				{ "stride", buffer.stride },
				{ "variables", nlohmann::json::array() },
			};
			for (const auto& variable : buffer.variables) {
				item["variables"].push_back(WriteVariable(variable));
			}
			data["structuredBuffers"].push_back(std::move(item));
		}
		return data;
	}

	bool ReadReflection(const nlohmann::json& data,
		Engine::ShaderReflectionInfo& outReflection) {

		if (!data.is_object()) {
			return false;
		}
		outReflection = Engine::ShaderReflectionInfo{};
		outReflection.requiresFlags = data.value("requiresFlags", uint64_t{ 0 });
		if (const auto found = data.find("threadGroup");
			found != data.end() && found->is_array() && found->size() == 3) {
			outReflection.threadGroupX = (*found)[0].get<UINT>();
			outReflection.threadGroupY = (*found)[1].get<UINT>();
			outReflection.threadGroupZ = (*found)[2].get<UINT>();
		}
		for (const nlohmann::json& item : data.value("resources", nlohmann::json::array())) {
			Engine::ShaderResourceBinding resource{};
			resource.name = item.value("name", "");
			resource.parameterID.value = item.value("parameterID", uint64_t{ 0 });
			resource.semantic = static_cast<Engine::MaterialParameterSemantic>(item.value("semantic", 0u));
			resource.kind = static_cast<Engine::ShaderBindingKind>(item.value("kind", 0u));
			resource.bindPoint = item.value("bindPoint", 0u);
			resource.bindCount = item.value("bindCount", 1u);
			resource.space = item.value("space", 0u);
			resource.stageMask = static_cast<Engine::ShaderStage>(item.value("stageMask", 0u));
			resource.rawType = static_cast<D3D_SHADER_INPUT_TYPE>(item.value("rawType", 0u));
			outReflection.resources.emplace_back(std::move(resource));
		}
		for (const nlohmann::json& item : data.value("inputs", nlohmann::json::array())) {
			Engine::ShaderInputSemantic input{};
			input.semanticName = item.value("semanticName", "");
			input.semanticIndex = item.value("semanticIndex", 0u);
			input.registerIndex = item.value("registerIndex", 0u);
			input.mask = static_cast<BYTE>(item.value("mask", 0u));
			input.componentType = static_cast<D3D_REGISTER_COMPONENT_TYPE>(item.value("componentType", 0u));
			outReflection.inputs.emplace_back(std::move(input));
		}
		for (const nlohmann::json& item : data.value("constantBuffers", nlohmann::json::array())) {
			Engine::ShaderConstantBufferInfo buffer{};
			buffer.name = item.value("name", "");
			buffer.bindPoint = item.value("bindPoint", 0u);
			buffer.space = item.value("space", 0u);
			buffer.size = item.value("size", 0u);
			for (const nlohmann::json& value : item.value("variables", nlohmann::json::array())) {
				Engine::ShaderConstantBufferVariable variable{};
				if (ReadVariable(value, variable)) {
					buffer.variables.emplace_back(std::move(variable));
				}
			}
			outReflection.constantBuffers.emplace_back(std::move(buffer));
		}
		for (const nlohmann::json& item : data.value("structuredBuffers", nlohmann::json::array())) {
			Engine::ShaderStructuredBufferInfo buffer{};
			buffer.name = item.value("name", "");
			buffer.bindPoint = item.value("bindPoint", 0u);
			buffer.space = item.value("space", 0u);
			buffer.stride = item.value("stride", 0u);
			for (const nlohmann::json& value : item.value("variables", nlohmann::json::array())) {
				Engine::ShaderConstantBufferVariable variable{};
				if (ReadVariable(value, variable)) {
					buffer.variables.emplace_back(std::move(variable));
				}
			}
			outReflection.structuredBuffers.emplace_back(std::move(buffer));
		}
		return true;
	}

	nlohmann::json WriteShaderMetadata(const Engine::ShaderAsset& asset) {

		nlohmann::json data = Engine::ToJson(asset);
		data["guid"] = Engine::ToString(asset.guid);
		return data;
	}

	bool ReadShaderMetadata(const nlohmann::json& data,
		Engine::ShaderAsset& outAsset) {

		if (!Engine::FromJson(data, outAsset)) {
			return false;
		}
		outAsset.guid = Engine::FromString32Hex(data.value("guid", ""));
		for (Engine::ShaderStageEntry& stage : outAsset.stages) {
			stage.ownerShader = outAsset.guid;
			stage.file = "cooked://" + Engine::ToString(outAsset.guid) + "/" +
				std::string(Engine::EnumAdapter<Engine::ShaderStage>::ToString(stage.stage));
		}
		return static_cast<bool>(outAsset.guid);
	}

	bool WriteBinary(const std::filesystem::path& path,
		const void* data, size_t size) {

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			return false;
		}
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return false;
		}
		stream.write(static_cast<const char*>(data),
			static_cast<std::streamsize>(size));
		return stream.good();
	}

	bool ReadBinary(const std::filesystem::path& path,
		std::vector<uint8_t>& outData) {

		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream.is_open()) {
			return false;
		}
		const std::streamsize size = stream.tellg();
		if (size <= 0) {
			return false;
		}
		outData.resize(static_cast<size_t>(size));
		stream.seekg(0, std::ios::beg);
		return stream.read(reinterpret_cast<char*>(outData.data()), size).good();
	}

	bool LoadRuntimeManifest() {

		const std::filesystem::path manifestPath =
			Engine::RuntimePaths::GetGameRoot() / kShaderCookManifest;
		if (g_runtimeState.loaded &&
			g_runtimeState.manifestPath == manifestPath) {
			return g_runtimeState.valid;
		}

		g_runtimeState = ShaderCookRuntimeState{};
		g_runtimeState.loaded = true;
		g_runtimeState.manifestPath = manifestPath;
		// Source実行ではCook済みデータを参照せず、呼び出し側のHLSLコンパイルへ戻す
		if (!Engine::ShaderCook::IsCookedProduct()) {
			return false;
		}
		const nlohmann::json manifest =
			Engine::JsonAdapter::Load(manifestPath, true);
		if (!manifest.is_object() ||
			manifest.value("schemaVersion", 0u) != kShaderCookSchemaVersion ||
			!manifest.value("cookedOnly", false)) {
			return false;
		}
		for (const nlohmann::json& item :
			manifest.value("shaders", nlohmann::json::array())) {
			CookedShaderRecord record{};
			if (!item.is_object() ||
				!ReadShaderMetadata(item.value("asset", nlohmann::json{}), record.asset)) {
				return false;
			}
			for (const nlohmann::json& stageData :
				item.value("stages", nlohmann::json::array())) {
				CookedStageRecord stage{};
				stage.request.shader = record.asset.guid;
				stage.request.stage = Engine::EnumAdapter<Engine::ShaderStage>::FromString(
					stageData.value("stage", "None")).value_or(Engine::ShaderStage::None);
				stage.request.entry = stageData.value("entry", "main");
				stage.request.profile = stageData.value("profile", "");
				stage.bytecodePath = manifestPath.parent_path() /
					Engine::Algorithm::PathFromUTF8(stageData.value("bytecode", ""));
				if (stage.request.stage == Engine::ShaderStage::None ||
					stage.bytecodePath.filename().empty() ||
					!ReadReflection(stageData.value("reflection", nlohmann::json{}), stage.reflection)) {
					return false;
				}
				record.stages.emplace_back(std::move(stage));
			}
			g_runtimeState.shaders.emplace(record.asset.guid, std::move(record));
		}
		for (const nlohmann::json& item :
			manifest.value("pipelines", nlohmann::json::array())) {
			Engine::RenderPipelineAsset pipeline{};
			if (!item.is_object() || !Engine::FromJson(item, pipeline)) {
				return false;
			}
			pipeline.guid = Engine::FromString32Hex(item.value("guid", ""));
			if (!pipeline.guid) {
				return false;
			}
			g_runtimeState.pipelines.emplace(pipeline.guid, std::move(pipeline));
		}
		g_runtimeState.valid = !g_runtimeState.shaders.empty();
		return g_runtimeState.valid;
	}
}

//============================================================================
//	ShaderCook classMethods
//============================================================================
bool Engine::ShaderCook::Cook(const std::filesystem::path& manifestPath,
	const std::filesystem::path& outputRoot,
	ShaderCookResult& outResult, std::string& outError) {

	outResult = ShaderCookResult{};
	outError.clear();
	std::error_code outputError;
	const std::filesystem::path resolvedOutputRoot =
		std::filesystem::absolute(outputRoot, outputError);
	if (outputError || resolvedOutputRoot.empty()) {
		outError = "シェーダーCookの出力先が不正です";
		return false;
	}
	const nlohmann::json buildManifest = JsonAdapter::Load(manifestPath, true);
	if (!buildManifest.is_object() ||
		buildManifest.value("schemaVersion", 0u) != 2u ||
		!buildManifest.contains("files") ||
		!buildManifest["files"].is_array()) {
		outError = "製品ビルドのマニフェストが不正です";
		return false;
	}

	const std::filesystem::path gameRoot = Algorithm::PathFromUTF8(
		buildManifest.value("gameRoot", std::string{}));
	std::error_code ec;
	std::filesystem::current_path(gameRoot, ec);
	if (ec) {
		outError = "ゲームプロジェクトのフォルダーが見つかりません";
		return false;
	}
	RuntimePaths::Refresh();
	AssetDatabase database{};
	if (!database.Init() || !database.RebuildMeta()) {
		outError = "アセットデータベースの初期化に失敗しました";
		return false;
	}

	std::unordered_set<std::wstring> includedFiles;
	for (const nlohmann::json& item : buildManifest["files"]) {
		const std::filesystem::path source = Algorithm::PathFromUTF8(
			item.value("source", std::string{}));
		if (!source.empty()) {
			includedFiles.insert(MakePathKey(source));
		}
	}

	std::vector<const AssetMeta*> shaderMetas;
	std::vector<const AssetMeta*> graphMetas;
	// 必須の組み込みアセットと依存先が収集一覧から欠けていればCookを開始しない
	std::vector<AssetID> requiredAssets(
		std::begin(BuiltinAssets::Runtime::Assets), std::end(BuiltinAssets::Runtime::Assets));
	std::unordered_set<AssetID> checkedRequiredAssets;
	for (size_t index = 0; index < requiredAssets.size(); ++index) {
		const AssetID assetID = requiredAssets[index];
		if (!checkedRequiredAssets.insert(assetID).second) continue;
		const AssetMeta* meta = database.Find(assetID);
		if (meta && meta->assetPath.starts_with("Engine/Assets/Shaders/Builtin/Editor/")) continue;
		const std::filesystem::path path = database.ResolveFullPath(assetID);
		std::filesystem::path metaPath = path;
		metaPath += L".meta";
		if (!meta || !includedFiles.contains(MakePathKey(path)) ||
			!includedFiles.contains(MakePathKey(metaPath)) ||
			!std::filesystem::is_regular_file(path, ec) || ec) {
			outError = "製品に必須のアセットが含まれていません GUID=" + ToString(assetID) +
				" path=" + Algorithm::PathToUTF8(path);
			return false;
		}
		for (const AssetID dependency : database.FindDependencies(assetID)) {
			requiredAssets.push_back(dependency);
		}
	}
	for (const auto& [assetID, meta] : database.GetAssets()) {
		if (!includedFiles.contains(MakePathKey(database.ResolveFullPath(assetID)))) {
			continue;
		}
		if (meta.type == AssetType::Shader) {
			// HLSLソースは定義ファイルのステージからコンパイルする
			const std::string extension = Algorithm::ToLower(
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(meta.assetPath).extension()));
			if (extension == ".hlsl" || extension == ".hlsli") {
				continue;
			}
			shaderMetas.emplace_back(&meta);
		} else if (meta.type == AssetType::ShaderGraph) {
			graphMetas.emplace_back(&meta);
		}
	}
	const auto sortByPath =
		[](const AssetMeta* lhs, const AssetMeta* rhs) {
			return lhs->assetPath < rhs->assetPath;
		};
	std::sort(shaderMetas.begin(), shaderMetas.end(), sortByPath);
	std::sort(graphMetas.begin(), graphMetas.end(), sortByPath);
	for (const AssetID shaderID : BuiltinAssets::Shaders::FixedRuntime) {
		if (std::none_of(shaderMetas.begin(), shaderMetas.end(),
			[shaderID](const AssetMeta* meta) { return meta->guid == shaderID; })) {
			outError = "製品描画に必須のシェーダーが含まれていません: " + ToString(shaderID);
			return false;
		}
	}

	DxShaderCompiler compiler{};
	compiler.Init();
	nlohmann::json cookedManifest = {
		{ "schemaVersion", kShaderCookSchemaVersion },
		{ "cookedOnly", true },
		{ "shaders", nlohmann::json::array() },
		{ "pipelines", nlohmann::json::array() },
	};
	const auto cookShader = [&](ShaderAsset shader,
		std::string_view sourceName) -> bool {

		if (shader.stages.empty()) {
			outError = "シェーダーに有効なステージがありません: " + std::string(sourceName);
			return false;
		}
		nlohmann::json shaderRecord = {
			{ "asset", WriteShaderMetadata(shader) },
			{ "stages", nlohmann::json::array() },
		};
		for (ShaderStageEntry& stage : shader.stages) {
			std::filesystem::path sourcePath{};
			if (const std::optional<AssetID> sourceID =
				TryParseAssetGUID32Hex(stage.file)) {
				sourcePath = database.ResolveFullPath(*sourceID);
			} else {
				const std::filesystem::path direct =
					Algorithm::PathFromUTF8(stage.file);
				std::error_code sourceError;
				sourcePath = std::filesystem::is_regular_file(direct, sourceError) ?
					direct : database.ResolveAssetPath(stage.file);
			}
			const std::string profile = stage.profile.empty() ?
				ResolveDefaultProfile(stage.stage) : stage.profile;
			const std::string entry = stage.entry.empty() ? "main" : stage.entry;
			CompiledShader compiled = compiler.CompileShader(
				sourcePath.wstring(), Algorithm::ConvertString(profile).c_str(),
				Algorithm::ConvertString(entry).c_str(), stage.stage);
			if (!compiled.IsValid()) {
				outError = "シェーダーのコンパイルに失敗しました: " + std::string(sourceName) + " [" +
					std::string(EnumAdapter<ShaderStage>::ToString(stage.stage)) + "]";
				return false;
			}
			ApplyShaderParameterMetadata(compiled.reflection, shader);
			const std::string fileName =
				ToString(shader.guid) + "/" +
				std::string(EnumAdapter<ShaderStage>::ToString(stage.stage)) +
				"_" + entry + ".dxil";
			const std::filesystem::path bytecodePath = resolvedOutputRoot /
				Algorithm::PathFromUTF8(fileName);
			if (!WriteBinary(bytecodePath, compiled.GetBytecodePointer(),
				compiled.GetBytecodeSize())) {
				outError = "Cook済みシェーダーの書き込みに失敗しました: " + fileName;
				return false;
			}
			shaderRecord["stages"].push_back({
				{ "stage", EnumAdapter<ShaderStage>::ToString(stage.stage) },
				{ "entry", entry },
				{ "profile", profile },
				{ "bytecode", fileName },
				{ "reflection", WriteReflection(compiled.reflection) },
			});
			++outResult.stageCount;
			outResult.bytecodeSize += compiled.GetBytecodeSize();
		}
		cookedManifest["shaders"].push_back(std::move(shaderRecord));
		++outResult.shaderCount;
		return true;
	};

	for (const AssetMeta* meta : shaderMetas) {
		ShaderAsset shader{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(meta->guid), true), shader)) {
			outError = "シェーダーアセットの読み込みに失敗しました: " + meta->assetPath;
			return false;
		}
		shader.guid = meta->guid;
		if (!cookShader(std::move(shader), meta->assetPath)) {
			return false;
		}
	}
	for (const AssetMeta* meta : graphMetas) {
		ShaderGraphAsset graph{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(meta->guid), true), graph)) {
			outError = "Shader Graphの読み込みに失敗しました: " + meta->assetPath;
			return false;
		}
		ShaderGraphArtifact artifact{};
		if (!ShaderGraphArtifactCache::Compile(
			graph, meta->guid, artifact, &database)) {
			outError = "Shader Graphのコンパイルに失敗しました: " + meta->assetPath;
			return false;
		}
		if (artifact.opaqueShader.guid &&
			!cookShader(std::move(artifact.opaqueShader), meta->assetPath)) {
			return false;
		}
		if (artifact.transparentShader.guid &&
			!cookShader(std::move(artifact.transparentShader), meta->assetPath)) {
			return false;
		}
		if (artifact.depthShader.guid &&
			!cookShader(std::move(artifact.depthShader), meta->assetPath)) {
			return false;
		}
		if (artifact.pickingShader.guid &&
			!cookShader(std::move(artifact.pickingShader), meta->assetPath)) {
			return false;
		}
		if (artifact.computeShader.guid &&
			!cookShader(std::move(artifact.computeShader), meta->assetPath)) {
			return false;
		}
		if (artifact.rayTracingShader.guid &&
			!cookShader(std::move(artifact.rayTracingShader), meta->assetPath)) {
			return false;
		}
		for (const RenderPipelineAsset* pipeline : {
			&artifact.opaquePipeline,
			&artifact.transparentPipeline,
			&artifact.depthPipeline,
			&artifact.pickingPipeline,
			&artifact.computePipeline,
			&artifact.rayTracingPipeline,
			}) {
			if (!pipeline->guid) {
				continue;
			}
			nlohmann::json pipelineData = ToJson(*pipeline);
			pipelineData["guid"] = ToString(pipeline->guid);
			cookedManifest["pipelines"].push_back(std::move(pipelineData));
		}
	}
	if (outResult.stageCount == 0) {
		outError = "製品ビルドにシェーダーアセットが含まれていません";
		return false;
	}

	// 必須Pipelineの製品用ステージもCook結果と照合する
	std::unordered_set<AssetID> cookedShaders;
	for (const auto& shader : cookedManifest["shaders"]) {
		cookedShaders.insert(FromString32Hex(shader.at("asset").at("guid").get<std::string>()));
	}
	for (const auto& [assetID, meta] : database.GetAssets()) {

		if (!checkedRequiredAssets.contains(assetID) || meta.type != AssetType::RenderPipeline ||
			!includedFiles.contains(MakePathKey(database.ResolveFullPath(assetID)))) {
			continue;
		}
		RenderPipelineAsset pipeline{};
		if (!FromJson(JsonAdapter::Load(database.ResolveFullPath(assetID), false), pipeline)) {
			outError = "配置するPipelineを読み込めません: " + meta.assetPath;
			return false;
		}
		for (const auto& variant : pipeline.variants) {
			const AssetMeta* shaderMeta = database.Find(variant.shader);
			if (shaderMeta && shaderMeta->assetPath.starts_with("Engine/Assets/Shaders/Builtin/Editor/")) {
				continue;
			}
			if (!cookedShaders.contains(variant.shader)) {
				outError = "PipelineのShaderがCookされていません: " + meta.assetPath +
					" Shader=" + ToString(variant.shader);
				return false;
			}
		}
	}
	if (!JsonAdapter::SaveCanonical(resolvedOutputRoot / "ShaderCookManifest.json",
		cookedManifest)) {
		outError = "シェーダーCookのマニフェストの書き込みに失敗しました";
		return false;
	}
	return true;
}

bool Engine::ShaderCook::IsCookedProduct() {

	return RuntimePaths::IsProductBuild();
}

bool Engine::ShaderCook::LoadShaderAsset(AssetID shaderID,
	ShaderAsset& outAsset) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto found = g_runtimeState.shaders.find(shaderID);
	if (found == g_runtimeState.shaders.end()) {
		return false;
	}
	outAsset = found->second.asset;
	return true;
}

bool Engine::ShaderCook::LoadPipelineAsset(AssetID pipelineID,
	RenderPipelineAsset& outAsset) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto found = g_runtimeState.pipelines.find(pipelineID);
	if (found == g_runtimeState.pipelines.end()) {
		return false;
	}
	outAsset = found->second;
	return true;
}

bool Engine::ShaderCook::Load(const ShaderCookRequest& request,
	CompiledShader& outShader) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto shader = g_runtimeState.shaders.find(request.shader);
	if (shader == g_runtimeState.shaders.end()) {
		return false;
	}
	const auto stage = std::find_if(shader->second.stages.begin(),
		shader->second.stages.end(), [&](const CookedStageRecord& record) {
			return record.request.stage == request.stage &&
				record.request.entry == request.entry &&
				record.request.profile == request.profile;
		});
	if (stage == shader->second.stages.end()) {
		return false;
	}
	outShader = CompiledShader{};
	outShader.stage = request.stage;
	outShader.entry = Algorithm::ConvertString(request.entry);
	outShader.profile = Algorithm::ConvertString(request.profile);
	outShader.reflection = stage->reflection;
	return ReadBinary(stage->bytecodePath, outShader.bytecode);
}
