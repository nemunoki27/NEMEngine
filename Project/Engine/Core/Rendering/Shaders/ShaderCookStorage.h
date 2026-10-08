#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderCook.h"

namespace Engine::ShaderCookStorage {

	constexpr uint32_t kShaderCookSchemaVersion = 1;

	// Reflectionを保存形式へ変換する
	nlohmann::json WriteReflection(
		const Engine::ShaderReflectionInfo& reflection);
	// Reflectionを読み込む
	bool ReadReflection(const nlohmann::json& data,
		Engine::ShaderReflectionInfo& outReflection);
	// Shader情報を保存形式へ変換する
	nlohmann::json WriteShaderMetadata(const Engine::ShaderAsset& asset);
	// Shader情報を読み込む
	bool ReadShaderMetadata(const nlohmann::json& data,
		Engine::ShaderAsset& outAsset);
	// Bytecodeを保存する
	bool WriteBinary(const std::filesystem::path& path,
		const void* data, size_t size);
	// Bytecodeを読み込む
	bool ReadBinary(const std::filesystem::path& path,
		std::vector<uint8_t>& outData);
}
