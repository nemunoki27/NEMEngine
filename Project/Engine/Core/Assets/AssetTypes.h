#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/IDentity/UUID.h>

// c++
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	AssetTypes
	//============================================================================
	using AssetID = UUID;

	// アセットの種類
	enum class AssetType {

		Unknown = 0,
		Prefab,
		Texture,
		Material,
		Mesh,
		Scene,
		Shader,
		RenderPipeline,
		Font,
		Script,
		Audio,
		AnimationClip,
		PostProcessStack,
		ParticleEffect,
	};

	// アセット参照の診断種別
	enum class AssetReferenceIssueType {

		None,
		InvalidFormat,  // 文字列はあるがUID形式として不正
		MissingAsset,   // 形式は正しいがDBに存在しない
		TypeMismatch,   // 存在するが期待型と異なる
	};
	// アセット参照の検証結果
	struct AssetReferenceDiagnostic {

		AssetReferenceIssueType issue = AssetReferenceIssueType::None;
		AssetID assetID{};
		AssetType expectedType = AssetType::Unknown;
		AssetType actualType = AssetType::Unknown;
	};

	// nlohmann::jsonからAssetIDを取得する、UID文字列のみで検証はしない
	AssetID ParseAssetID(const nlohmann::json& in, const char* key);
	// GUID文字列またはGUIDフィールドを持つオブジェクトからAssetIDを取得する
	// databaseが渡されれば存在確認、expectedTypeがUnknown以外なら型確認も行い、
	// diagnosticが渡されれば結果を書き込む(不一致でも参照値自体は返す)
	AssetID ParseAssetReference(const nlohmann::json& in, const char* key,
		const AssetDatabase* database, AssetType expectedType,
		AssetReferenceDiagnostic* diagnostic = nullptr);
	// 参照JSONをGUID文字列として書き出す(無効値は空文字列)
	nlohmann::json ToAssetReferenceJson(AssetID assetID);
} // Engine
