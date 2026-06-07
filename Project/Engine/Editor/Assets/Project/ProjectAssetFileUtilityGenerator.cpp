#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <format>
#include <iterator>

namespace Engine {

	const char* ProjectAssetFileUtility::GetFileSuffix(ProjectAssetFileKind kind) {
		// ファイルの種類に応じた拡張子を返す
		switch (kind) {
		case ProjectAssetFileKind::Text: return ".txt";
		case ProjectAssetFileKind::Script: return ".cs";
		case ProjectAssetFileKind::Scene: return ".scene.json";
		case ProjectAssetFileKind::Prefab: return ".prefab.json";
		case ProjectAssetFileKind::Material: return ".material.json";
		case ProjectAssetFileKind::AnimationClip: return ".animClip.json";
		case ProjectAssetFileKind::Shader: return ".shader.json";
		case ProjectAssetFileKind::RenderPipeline: return ".pipeline.json";
		case ProjectAssetFileKind::Folder:
		default: break;
		}
		return "";
	}

	std::string ProjectAssetFileUtility::BuildFileContent(ProjectAssetFileKind kind, const std::string& assetName) {
		// 新規作成されるファイルの中身を雛形から生成
		switch (kind) {
		case ProjectAssetFileKind::Script:
		{
			// C#スクリプト。csprojから名前空間を取得し、ファイル名から有効なクラス名を生成して適用
			const std::string rootNamespace = LoadGameScriptRootNamespace();
			const std::string className = MakeCSharpClassName(assetName);
			return std::format("using NEMEngine;\n\nnamespace {};\n\npublic sealed class {} : ScriptBehaviour\n{{\n\tpublic override void Start()\n\t{{\n\t}}\n\n\tpublic override void Update()\n\t{{\n\t}}\n}}\n", rootNamespace, className);
		}
		case ProjectAssetFileKind::Scene:
			// シーンファイル。最低限のヘッダーと空のEntityリストを持つJSON
			return std::format("{{\n  \"Header\": {{\n    \"guid\": \"\",\n    \"name\": \"{}\",\n    \"subScenes\": []\n  }},\n  \"Entities\": []\n}}\n", assetName);
		case ProjectAssetFileKind::Prefab:
			// プレファイル。シーンと同様だがPrefab固有のメタ情報を含む
			return std::format("{{\n  \"Header\": {{\n    \"guid\": \"\",\n    \"name\": \"{}\",\n    \"rootLocalFileID\": \"\",\n    \"version\": 1\n  }},\n  \"Entities\": []\n}}\n", assetName);
		case ProjectAssetFileKind::Material:
			// マテリアル。標準のシェーダとパラメータを設定した状態で作成
			return std::format("{{\n  \"name\": \"{}\",\n  \"domain\": \"Surface\",\n  \"passes\": [\n    {{\n      \"passKind\": \"ZPrepass\",\n      \"pipeline\": \"f09836087840b1d2\",\n      \"preferredVariant\": \"GraphicsMesh\"\n    }},\n    {{\n      \"passKind\": \"Draw\",\n      \"pipeline\": \"966f3e8a34595313\",\n      \"preferredVariant\": \"GraphicsMesh\"\n    }}\n  ],\n  \"parameters\": {{\n    \"BaseColor\": {{ \"r\": 1.0, \"g\": 1.0, \"b\": 1.0, \"a\": 1.0 }},\n    \"Metallic\": 0.0,\n    \"Roughness\": 0.5\n  }}\n}}\n", assetName);
		case ProjectAssetFileKind::AnimationClip:
			return std::format("{{\n  \"guid\": \"\",\n  \"name\": \"{}\",\n  \"duration\": 1.0,\n  \"curveTracks\": [],\n  \"eventTracks\": []\n}}\n", assetName);
		case ProjectAssetFileKind::Shader:
			return std::format("{{\n  \"name\": \"{}\",\n  \"stages\": []\n}}\n", assetName);
		case ProjectAssetFileKind::RenderPipeline:
			return std::format("{{\n  \"name\": \"{}\",\n  \"variants\": []\n}}\n", assetName);
		case ProjectAssetFileKind::Text: return "";
		case ProjectAssetFileKind::Folder:
		default: break;
		}
		return "";
	}

	bool ProjectAssetFileUtility::WriteTextFile(const std::filesystem::path& path, const std::string& content) {
		// テキストファイルをバイナリモードで開き、内容を書き込む。LF/CRLFの混在を防ぐためtrunc指定
		std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) { return false; }
		ofs << content;
		return true;
	}

	std::string ProjectAssetFileUtility::LoadGameScriptRootNamespace() {
		// ゲーム側csprojを読み取り、デフォルトの名前空間(<RootNamespace>)を抽出
		const std::filesystem::path csproj = RuntimePaths::GetGameRoot() / "Scripts/GameScripts.csproj";
		std::ifstream ifs(csproj);
		if (!ifs.is_open()) { return "GameScripts"; }
		std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		
		const std::string beginTag = "<RootNamespace>";
		const std::string endTag = "</RootNamespace>";
		const size_t begin = text.find(beginTag);
		if (begin == std::string::npos) { return "GameScripts"; }
		
		const size_t valueBegin = begin + beginTag.size();
		const size_t end = text.find(endTag, valueBegin);
		if (end == std::string::npos) { return "GameScripts"; }
		
		std::string rootNamespace = text.substr(valueBegin, end - valueBegin);
		return rootNamespace.empty() ? "GameScripts" : rootNamespace;
	}

} // Engine
