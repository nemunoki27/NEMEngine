#include "ShaderCookCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ShaderCookStorage.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

using namespace Engine::ShaderCookStorage;

namespace {

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
}

Engine::ShaderCookCompiler::ShaderCookCompiler(AssetDatabase& database, const std::filesystem::path& outputRoot) :
	database_(database), outputRoot_(outputRoot) {

	compiler_.Init();
}

bool Engine::ShaderCookCompiler::Cook(ShaderAsset shader, std::string_view sourceName, nlohmann::json& cookedManifest,
	ShaderCookResult& outResult, std::string& outError) {

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
			sourcePath = database_.ResolveFullPath(*sourceID);
		} else {
			const std::filesystem::path direct =
				Algorithm::PathFromUTF8(stage.file);
			std::error_code sourceError;
			sourcePath = std::filesystem::is_regular_file(direct, sourceError) ?
				direct : database_.ResolveAssetPath(stage.file);
		}
		const std::string profile = stage.profile.empty() ?
			ResolveDefaultProfile(stage.stage) : stage.profile;
		const std::string entry = stage.entry.empty() ? "main" : stage.entry;
		CompiledShader compiled = compiler_.CompileShader(
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
		const std::filesystem::path bytecodePath = outputRoot_ /
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
	}
