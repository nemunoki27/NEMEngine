#include "DxShaderCache.h"
#include "DxShaderReflectionParser.h"
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <json.hpp>

#include <array>
#include <cstring>
#include <fstream>

using namespace Engine;

namespace {

	struct CacheBucket {
		std::mutex mutex;
		std::filesystem::path path;
		std::string data;
	};
	std::array<CacheBucket, 32> buckets;

	std::string Hash(std::string_view bytes) {

		return ContentHash::SHA256({ reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size() });
	}

	bool MatchesDependencies(const nlohmann::json& dependencies) {

		for (const auto& item : dependencies) {
			const auto path = Algorithm::PathFromUTF8(item.at(0).get<std::string>());
			const auto& expected = item.at(1).get_ref<const std::string&>();
			std::error_code error;
			if (expected.empty()) {
				if (std::filesystem::exists(path, error) || error) return false;
			} else if (ContentHash::FileSHA256(path) != expected) return false;
		}
		return true;
	}
}

DxShaderCache::DxShaderCache(const std::filesystem::path& directory, std::string_view compilerHash,
	std::span<LPCWSTR const> arguments, ShaderStage stage, const DxcBuffer& source) {

	if (directory.empty() || compilerHash.empty()) return;
	std::string inputs = "NEM-DXIL-1/" + std::string(compilerHash) + "/" + std::to_string(static_cast<unsigned>(stage));
	for (const auto* argument : arguments) {
		inputs.append(reinterpret_cast<const char*>(argument), (std::wcslen(argument) + 1) * sizeof(wchar_t));
	}
	const auto key = Hash(inputs);
	sourceHash_ = ContentHash::SHA256({ static_cast<const uint8_t*>(source.Ptr), source.Size });
	if (key.empty() || sourceHash_.empty()) return;
	path_ = directory / (key + ".dxcache");
	bucket_ = std::hash<std::filesystem::path>{}(path_) % buckets.size();
	lock_ = std::unique_lock(buckets[bucket_].mutex);
}

bool DxShaderCache::Load(IDxcUtils* utils, CompiledShader& shader) {

	if (path_.empty()) return false;
	auto& bucket = buckets[bucket_];
	std::string diskData;
	std::string_view data;
	if (bucket.path == path_) {
		data = bucket.data;
	} else {
		std::error_code error;
		const auto size = std::filesystem::file_size(path_, error);
		if (error || size < 66 || size > 128 * 1024 * 1024) return false;
		std::ifstream stream(path_, std::ios::binary);
		diskData.resize(static_cast<size_t>(size));
		if (!stream.read(diskData.data(), static_cast<std::streamsize>(size))) return false;
		data = diskData;
		if (Hash(data.substr(64)) != data.substr(0, 64)) return false;
	}
	const auto split = data.find('\n', 64);
	if (split == data.npos) return false;
	try {
		const auto header = nlohmann::json::parse(data.substr(64, split - 64));
		if (header.at("key") != path_.stem().string() || header.at("source") != sourceHash_ ||
			!MatchesDependencies(header.at("includes"))) return false;
		const auto payload = data.substr(split + 1);
		const size_t reflectionSize = header.at("reflection").get<size_t>();
		if (reflectionSize >= payload.size() || payload.size() > UINT32_MAX) return false;
		ShaderReflectionInfo reflection{};
		if (reflectionSize) {
			const DxcBuffer buffer{ payload.data(), reflectionSize, 0 };
			if (!ParseDxShaderReflection(utils, buffer, shader.stage, reflection)) return false;
		}
		const auto object = payload.substr(reflectionSize);
		ComPtr<IDxcBlobEncoding> blob;
		if (FAILED(utils->CreateBlob(object.data(), static_cast<UINT32>(object.size()), 0, &blob))) return false;
		shader.diagnostics = header.at("diagnostics").get<std::string>();
		shader.object = blob;
		shader.reflection = std::move(reflection);
	} catch (const std::exception&) { return false; }
	if (!diskData.empty() && diskData.size() <= 2 * 1024 * 1024) {
		bucket.path = path_;
		bucket.data = std::move(diskData);
	}
	return true;
}

void DxShaderCache::Store(const CompiledShader& shader, IDxcBlob* reflection) {

	if (path_.empty() || !shader.IsValid()) return;
	nlohmann::json includes = nlohmann::json::array();
	for (const auto& [path, hash] : dependencies) {
		includes.push_back({ Algorithm::PathToUTF8(path), hash });
	}
	const nlohmann::json header = {
		{ "key", path_.stem().string() }, { "source", sourceHash_ }, { "includes", std::move(includes) },
		{ "reflection", reflection ? reflection->GetBufferSize() : 0 }, { "diagnostics", shader.diagnostics }
	};
	std::string data = header.dump() + "\n";
	if (reflection) data.append(static_cast<const char*>(reflection->GetBufferPointer()), reflection->GetBufferSize());
	data.append(static_cast<const char*>(shader.GetBytecodePointer()), shader.GetBytecodeSize());
	const auto checksum = Hash(data);
	if (checksum.empty()) return;
	data.insert(0, checksum);
	std::error_code error;
	std::filesystem::create_directories(path_.parent_path(), error);
	if (!error) StorageFileUtility::WriteBytes(path_, data);
	auto& bucket = buckets[bucket_];
	if (data.size() <= 2 * 1024 * 1024) {
		bucket.path = path_;
		bucket.data = std::move(data);
	} else {
		bucket.path.clear();
		bucket.data.clear();
	}
}
