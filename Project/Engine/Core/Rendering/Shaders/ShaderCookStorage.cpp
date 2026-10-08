#include "ShaderCookStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <fstream>

namespace {
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
}

namespace Engine::ShaderCookStorage {
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
}
