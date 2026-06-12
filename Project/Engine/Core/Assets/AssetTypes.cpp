#include "AssetTypes.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <string>

namespace {

	// 文字列または{ "guid": "..." }等のオブジェクトから参照文字列を取り出す
	std::string ReadReferenceText(const nlohmann::json& value) {

		if (value.is_string()) {
			return value.get<std::string>();
		}
		if (!value.is_object()) {
			return {};
		}

		for (const char* key : { "guid", "assetGuid", "asset", "id" }) {

			const std::string text = value.value(key, "");
			if (!text.empty()) {
				return text;
			}
		}
		return {};
	}
}

//============================================================================
//	AssetTypes classMethods
//============================================================================
Engine::AssetID Engine::ParseAssetID(const nlohmann::json& in, const char* key) {

	const std::string value = in.value(key, "");
	return value.empty() ? Engine::AssetID{} : Engine::FromString16Hex(value);
}

Engine::AssetID Engine::ParseAssetReference(const nlohmann::json& in, const char* key,
	const AssetDatabase* database, AssetType expectedType, AssetReferenceDiagnostic* diagnostic) {

	if (diagnostic) {
		*diagnostic = AssetReferenceDiagnostic{};
		diagnostic->expectedType = expectedType;
	}

	std::string value{};
	if (in.contains(key)) {
		value = ReadReferenceText(in.at(key));
	}
	// 空は「参照なし」として無効値を返す(診断対象にしない)
	if (value.empty()) {
		return AssetID{};
	}

	// UID文字列を厳密に解釈する
	const std::optional<AssetID> parsed = TryParseUUID16Hex(value);
	if (!parsed) {
		if (diagnostic) {
			diagnostic->issue = AssetReferenceIssueType::InvalidFormat;
		}
		return AssetID{};
	}

	const AssetID id = *parsed;
	if (diagnostic) {
		diagnostic->assetID = id;
	}

	// databaseが無ければ存在・型確認はしない
	if (!database) {
		return id;
	}

	const AssetMeta* meta = database->Find(id);
	if (!meta) {
		// 形式は正しいが実体が無いため参照値は保持しつつMissingとして診断する
		if (diagnostic) {
			diagnostic->issue = AssetReferenceIssueType::MissingAsset;
		}
		return id;
	}

	if (expectedType != AssetType::Unknown && meta->type != expectedType) {
		if (diagnostic) {
			diagnostic->issue = AssetReferenceIssueType::TypeMismatch;
			diagnostic->actualType = meta->type;
		}
		return id;
	}

	if (diagnostic) {
		diagnostic->actualType = meta->type;
	}
	return id;
}

nlohmann::json Engine::ToAssetReferenceJson(AssetID assetID) {

	return assetID ? nlohmann::json(ToString(assetID)) : nlohmann::json("");
}
