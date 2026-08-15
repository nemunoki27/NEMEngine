#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>

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
		case ProjectAssetFileKind::ShaderGraph: return ".shadergraph.json";
		case ProjectAssetFileKind::RenderFeatureProfile:
			return ".renderFeatureProfile.json";
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
			// C#スクリプトでcsprojから名前空間を取得しファイル名から有効なクラス名を生成して適用
			const std::string rootNamespace = LoadGameScriptRootNamespace();
			const std::string className = MakeCSharpClassName(assetName);
			return std::format(
				"using NEMEngine;\n"
				"\n"
				"namespace {0};\n"
				"\n"
				"//============================================================================\n"
				"//\t{1}\n"
				"//============================================================================\n"
				"public sealed class {1} : ScriptBehaviour\n"
				"{{\n"
				"\n"
				"    //========================================================================\n"
				"    //\t開始時処理\n"
				"    //========================================================================\n"
				"    public override void Start()\n"
				"    {{\n"
				"    }}\n"
				"\n"
				"    //========================================================================\n"
				"    //\t毎フレーム更新処理\n"
				"    //========================================================================\n"
				"    public override void Update()\n"
				"    {{\n"
				"    }}\n"
				"}}\n",
				rootNamespace, className);
		}
		case ProjectAssetFileKind::Scene:
			return std::format(
				"{{\n"
				"  \"ExternalActors\": [],\n"
				"  \"Header\": {{\n"
				"    \"name\": \"{}\",\n"
				"    \"renderFeatureProfile\": \"\",\n"
				"    \"subScenes\": []\n"
				"  }},\n"
				"  \"PrefabInstances\": [],\n"
				"  \"SchemaVersion\": 3\n"
				"}}\n", assetName);
		case ProjectAssetFileKind::Prefab:
			// プレファイルでシーンと同様だがPrefab固有のメタ情報を含む
			return std::format("{{\n  \"Entities\": [],\n  \"Header\": {{\n    \"guid\": \"\",\n    \"name\": \"{}\",\n    \"rootLocalFileID\": \"\",\n    \"version\": 1\n  }},\n  \"SchemaVersion\": 1\n}}\n", assetName);
		case ProjectAssetFileKind::Material:
		{
			// 標準PBRの型付き既定値からマテリアル雛形を生成する
			return JsonAdapter::SerializeCanonical(
				ToJson(CreateDefaultMeshMaterialAsset(assetName)), 2);
		}
		case ProjectAssetFileKind::AnimationClip:
			return std::format("{{\n  \"guid\": \"\",\n  \"name\": \"{}\",\n  \"duration\": 1.0,\n  \"curveTracks\": [],\n  \"events\": []\n}}\n", assetName);
		case ProjectAssetFileKind::Shader:
			return std::format("{{\n  \"name\": \"{}\",\n  \"stages\": []\n}}\n", assetName);
		case ProjectAssetFileKind::RenderPipeline:
			return std::format("{{\n  \"name\": \"{}\",\n  \"variants\": []\n}}\n", assetName);
		case ProjectAssetFileKind::ShaderGraph:
			// 新規グラフは標準PBRノードを接続済みの状態で作成する
			return JsonAdapter::SerializeCanonical(
				ToJson(CreateDefaultSurfaceShaderGraph(assetName)), 2);
		case ProjectAssetFileKind::RenderFeatureProfile:
		{
			RenderFeatureProfileAsset profile{};
			profile.name = assetName;
			return JsonAdapter::SerializeCanonical(
				RenderFeatureProfileSerializer::ToJson(profile), 2);
		}
		case ProjectAssetFileKind::Text: return "";
		case ProjectAssetFileKind::Folder:
		default: break;
		}
		return "";
	}

	bool ProjectAssetFileUtility::WriteTextFile(const std::filesystem::path& path, const std::string& content) {
		// テキストファイルをバイナリモードで開き内容を書き込む、LF/CRLFの混在を防ぐためtrunc指定
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
