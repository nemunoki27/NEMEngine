#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Utility/Json/JsonAdapter.h>

// c++
#include <array>
#include <cctype>
#include <fstream>
#include <format>
#include <iterator>
#include <system_error>
#include <vector>

//============================================================================
//	ProjectAssetFileUtility classMethods
//============================================================================

namespace {

	constexpr std::array<const char*, 7> kCompoundSuffixes = {
		".scene.json",
		".prefab.json",
		".material.json",
		".anim.json",
		".shader.json",
		".pipeline.json",
		".graph.json",
	};

	// 文字列前後の空白を取り除く
	std::string Trim(std::string text) {

		auto isSpace = [](unsigned char c) {
			return std::isspace(c) != 0;
			};

		while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
			text.erase(text.begin());
		}
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
			text.pop_back();
		}
		return text;
	}
	// ファイル名として使えない文字を置き換える
	std::string SanitizeFileName(std::string text) {

		text = Trim(std::move(text));
		for (char& c : text) {

			switch (c) {
			case '<':
			case '>':
			case ':':
			case '"':
			case '/':
			case '\\':
			case '|':
			case '?':
			case '*':
				c = '_';
				break;
			}
		}
		if (text.empty() || text == "." || text == "..") {
			return "NewAsset";
		}
		return text;
	}
	// C#のクラス名として使える形に変換する
	std::string MakeCSharpClassName(const std::string& fileName) {

		std::string result;
		bool upperNext = true;
		for (unsigned char c : fileName) {

			if (std::isalnum(c) || c == '_') {

				char out = static_cast<char>(c);
				if (upperNext && std::isalpha(c)) {
					out = static_cast<char>(std::toupper(c));
				}
				result.push_back(out);
				upperNext = false;
			} else {

				upperNext = true;
			}
		}
		if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
			result = "New" + result;
		}
		return result;
	}
	// 複合拡張子を考慮してベース名と拡張子に分離する
	std::pair<std::string, std::string> SplitAssetFileName(const std::filesystem::path& path) {

		const std::string fileName = path.filename().string();
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		for (const char* suffix : kCompoundSuffixes) {

			const std::string suffixText = suffix;
			if (Engine::Algorithm::EndsWith(lower, suffixText)) {
				return { fileName.substr(0, fileName.size() - suffixText.size()), suffixText };
			}
		}
		return { path.stem().string(), path.extension().string() };
	}
	// 指定パスが存在する場合、末尾に番号を付けて空いているパスを作る
	std::filesystem::path MakeUniquePath(const std::filesystem::path& preferredPath) {

		if (!std::filesystem::exists(preferredPath)) {
			return preferredPath;
		}

		const auto [baseName, suffix] = SplitAssetFileName(preferredPath);
		const std::filesystem::path directory = preferredPath.parent_path();

		for (uint32_t index = 1; index < 10000; ++index) {

			std::filesystem::path candidate = directory / std::format("{} {}{}", baseName, index, suffix);
			if (!std::filesystem::exists(candidate)) {
				return candidate;
			}
		}
		return {};
	}
	// 指定拡張子が既に入力されている場合は取り除く
	std::string RemoveTypedSuffix(std::string name, const char* suffix) {

		const std::string lowerName = Engine::Algorithm::ToLower(name);
		const std::string lowerSuffix = Engine::Algorithm::ToLower(suffix);
		if (!lowerSuffix.empty() && Engine::Algorithm::EndsWith(lowerName, lowerSuffix)) {
			name.resize(name.size() - lowerSuffix.size());
		}
		return name;
	}
	// パスが相対パスとして安全かを確認する
	bool IsSafeRelativePath(const std::filesystem::path& path) {

		if (path.empty()) {
			return true;
		}
		for (const auto& part : path) {
			if (part == "..") {
				return false;
			}
		}
		return true;
	}
	// RuntimePathsからアセットパスを取得する
	std::string ToAssetPath(const std::filesystem::path& fullPath) {

		return Engine::RuntimePaths::ToAssetPath(fullPath);
	}
	// GameScripts.csprojからRootNamespaceを読み取る
	std::string LoadGameScriptRootNamespace() {

		const std::filesystem::path csproj = Engine::RuntimePaths::GetGameRoot() / "Scripts/GameScripts.csproj";
		std::ifstream ifs(csproj);
		if (!ifs.is_open()) {
			return "GameScripts";
		}

		std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		const std::string beginTag = "<RootNamespace>";
		const std::string endTag = "</RootNamespace>";
		const size_t begin = text.find(beginTag);
		if (begin == std::string::npos) {
			return "GameScripts";
		}
		const size_t valueBegin = begin + beginTag.size();
		const size_t end = text.find(endTag, valueBegin);
		if (end == std::string::npos) {
			return "GameScripts";
		}
		std::string rootNamespace = Trim(text.substr(valueBegin, end - valueBegin));
		return rootNamespace.empty() ? "GameScripts" : rootNamespace;
	}
	// ファイルへ文字列を書き込む
	bool WriteTextFile(const std::filesystem::path& path, const std::string& content) {

		std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) {
			return false;
		}
		ofs << content;
		return true;
	}
	// metaファイルは複製対象から外す
	bool ShouldSkipCopyFile(const std::filesystem::path& path) {

		const std::string fileName = Engine::Algorithm::ToLower(path.filename().string());
		return Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos;
	}
	// アセットに対応するmetaファイルの候補を作る
	std::filesystem::path MakeMetaPath(const std::filesystem::path& path) {

		return path.string() + ".meta";
	}
	// 付属ファイルを移動または削除するときの候補を作る
	std::vector<std::filesystem::path> BuildAssetSidecarPaths(const Engine::ProjectAssetEntry& asset,
		const std::filesystem::path& assetPath) {

		std::vector<std::filesystem::path> result;
		result.emplace_back(MakeMetaPath(assetPath));
		for (const std::string& sidecar : asset.sidecarFiles) {
			result.emplace_back(assetPath.parent_path() / sidecar);
		}
		return result;
	}
	// パスが指定ディレクトリの中にあるか
	bool IsSameOrChildPath(const std::filesystem::path& path, const std::filesystem::path& parent) {

		const std::filesystem::path normalizedPath = path.lexically_normal();
		const std::filesystem::path normalizedParent = parent.lexically_normal();

		auto pathIt = normalizedPath.begin();
		auto parentIt = normalizedParent.begin();
		for (; parentIt != normalizedParent.end(); ++parentIt, ++pathIt) {
			if (pathIt == normalizedPath.end() || *pathIt != *parentIt) {
				return false;
			}
		}
		return true;
	}
	// 複製したJSONアセットの内部IDと表示名を新しいファイル名に合わせる
	void PatchDuplicatedJsonAsset(const std::filesystem::path& path, Engine::AssetType type) {

		if (type != Engine::AssetType::Scene &&
			type != Engine::AssetType::Prefab &&
			type != Engine::AssetType::Material &&
			type != Engine::AssetType::Animation &&
			type != Engine::AssetType::Shader &&
			type != Engine::AssetType::RenderPipeline) {
			return;
		}

		nlohmann::json data = Engine::JsonAdapter::Load(path.string(), false);
		if (!data.is_object()) {
			return;
		}

		const std::string assetName = SplitAssetFileName(path).first;
		if (type == Engine::AssetType::Scene || type == Engine::AssetType::Prefab) {

			nlohmann::json& header = data["Header"];
			if (!header.is_object()) {
				header = nlohmann::json::object();
			}
			header["guid"] = "";
			header["name"] = assetName;
		} else {

			data["guid"] = "";
			data["name"] = assetName;
		}
		Engine::JsonAdapter::Save(path.string(), data);
	}
}

const char* Engine::ProjectAssetFileUtility::GetCreateMenuLabel(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder:
		return "Folder";
	case ProjectAssetFileKind::Text:
		return "Text File";
	case ProjectAssetFileKind::Script:
		return "C# Script";
	case ProjectAssetFileKind::Scene:
		return "Scene";
	case ProjectAssetFileKind::Prefab:
		return "Prefab";
	case ProjectAssetFileKind::Material:
		return "Material";
	case ProjectAssetFileKind::Animation:
		return "Animation";
	case ProjectAssetFileKind::Shader:
		return "Shader";
	case ProjectAssetFileKind::RenderPipeline:
		return "Render Pipeline";
	}
	return "Asset";
}

const char* Engine::ProjectAssetFileUtility::GetDefaultName(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder:
		return "New Folder";
	case ProjectAssetFileKind::Text:
		return "New Text";
	case ProjectAssetFileKind::Script:
		return "NewScript";
	case ProjectAssetFileKind::Scene:
		return "NewScene";
	case ProjectAssetFileKind::Prefab:
		return "NewPrefab";
	case ProjectAssetFileKind::Material:
		return "NewMaterial";
	case ProjectAssetFileKind::Animation:
		return "NewAnimation";
	case ProjectAssetFileKind::Shader:
		return "NewShader";
	case ProjectAssetFileKind::RenderPipeline:
		return "NewPipeline";
	}
	return "NewAsset";
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::Create(ProjectAssetSource source,
	const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName) {

	ProjectAssetFileResult result{};
	result.isDirectory = kind == ProjectAssetFileKind::Folder;

	const std::filesystem::path directory = ResolveVirtualDirectory(source, directoryVirtualPath);
	if (directory.empty()) {
		result.message = "Invalid directory path.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(directory, ec);
	if (ec) {
		result.message = "Failed to create directory.";
		return result;
	}

	const char* suffix = GetFileSuffix(kind);
	std::string baseName = SanitizeFileName(requestedName.empty() ? GetDefaultName(kind) : requestedName);
	if (kind != ProjectAssetFileKind::Folder) {
		baseName = RemoveTypedSuffix(baseName, suffix);
	}

	const std::filesystem::path preferredPath = kind == ProjectAssetFileKind::Folder ?
		directory / baseName :
		directory / (baseName + suffix);
	const std::filesystem::path createPath = MakeUniquePath(preferredPath);
	if (createPath.empty()) {
		result.message = "Failed to build unique file path.";
		return result;
	}

	if (kind == ProjectAssetFileKind::Folder) {

		std::filesystem::create_directories(createPath, ec);
		if (ec) {
			result.message = "Failed to create folder.";
			return result;
		}
	} else {

		if (!WriteTextFile(createPath, BuildFileContent(kind, baseName))) {
			result.message = "Failed to write asset file.";
			return result;
		}
	}

	result.success = true;
	result.fullPath = createPath;
	result.assetPath = ToAssetPath(createPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateAsset(const ProjectAssetEntry& asset) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(sourcePath);
	if (targetPath.empty()) {
		result.message = "Failed to build duplicate file path.";
		return result;
	}

	std::error_code ec;
	std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, ec);
	if (ec) {
		result.message = "Failed to copy asset file.";
		return result;
	}

	PatchDuplicatedJsonAsset(targetPath, asset.type);

	// モデルなどの付属ファイルは、新しい本体名に合わせて複製する
	for (const std::string& sidecar : asset.sidecarFiles) {

		const std::filesystem::path sidecarSource = sourcePath.parent_path() / sidecar;
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget =
			targetPath.parent_path() / (targetPath.stem().string() + sidecarSource.extension().string());
		std::filesystem::copy_file(sidecarSource, sidecarTarget, std::filesystem::copy_options::none, ec);
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, directoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(sourcePath);
	if (targetPath.empty()) {
		result.message = "Failed to build duplicate folder path.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetPath, ec);
	if (ec) {
		result.message = "Failed to create duplicate folder.";
		return result;
	}

	for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath, ec)) {

		if (ec) {
			result.message = "Failed to scan source folder.";
			return result;
		}

		const std::filesystem::path relative = std::filesystem::relative(entry.path(), sourcePath, ec);
		if (ec || !IsSafeRelativePath(relative)) {
			continue;
		}

		const std::filesystem::path destination = targetPath / relative;
		if (entry.is_directory()) {

			std::filesystem::create_directories(destination, ec);
		} else if (entry.is_regular_file() && !ShouldSkipCopyFile(entry.path())) {

			std::filesystem::create_directories(destination.parent_path(), ec);
			std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::none, ec);
		}
		if (ec) {
			result.message = "Failed to copy folder contents.";
			return result;
		}
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteAsset(const ProjectAssetEntry& asset) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	std::error_code ec;
	std::filesystem::remove(sourcePath, ec);
	if (ec) {
		result.message = "Failed to delete asset file.";
		return result;
	}

	for (const std::filesystem::path& sidecar : BuildAssetSidecarPaths(asset, sourcePath)) {
		if (std::filesystem::exists(sidecar)) {
			std::filesystem::remove(sidecar, ec);
		}
	}

	result.success = true;
	result.assetPath = asset.assetPath;
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, directoryVirtualPath);
	const std::filesystem::path rootPath = GetSourceRoot(source);
	if (sourcePath.empty() || sourcePath == rootPath || !std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found or cannot be deleted.";
		return result;
	}

	std::error_code ec;
	std::filesystem::remove_all(sourcePath, ec);
	if (ec) {
		result.message = "Failed to delete folder.";
		return result;
	}

	result.success = true;
	result.assetPath = ToAssetPath(sourcePath.parent_path());
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveAsset(const ProjectAssetEntry& asset,
	ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || targetDirectory.empty()) {
		result.message = "Source asset or target folder was not found.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build move target path.";
		return result;
	}

	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to move asset file.";
		return result;
	}

	for (const std::filesystem::path& sidecarSource : BuildAssetSidecarPaths(asset, sourcePath)) {
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget = sidecarSource == MakeMetaPath(sourcePath) ?
			MakeMetaPath(targetPath) :
			targetPath.parent_path() / sidecarSource.filename();
		std::filesystem::rename(sidecarSource, sidecarTarget, ec);
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveDirectory(ProjectAssetSource source,
	const std::string& sourceDirectoryVirtualPath, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, sourceDirectoryVirtualPath);
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(source, targetDirectoryVirtualPath);
	const std::filesystem::path rootPath = GetSourceRoot(source);
	if (sourcePath.empty() || targetDirectory.empty() || sourcePath == rootPath ||
		!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source or target folder was not found.";
		return result;
	}
	if (IsSameOrChildPath(targetDirectory, sourcePath)) {
		result.message = "Cannot move a folder into itself.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build move target path.";
		return result;
	}

	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to move folder.";
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

std::filesystem::path Engine::ProjectAssetFileUtility::GetSourceRoot(ProjectAssetSource source) {

	switch (source) {
	case ProjectAssetSource::Engine:
		return RuntimePaths::GetEngineAssetsRoot();
	case ProjectAssetSource::Game:
		return RuntimePaths::GetGameRoot() / "GameAssets";
	}
	return RuntimePaths::GetEngineAssetsRoot();
}

std::filesystem::path Engine::ProjectAssetFileUtility::ResolveVirtualDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath) {

	const std::filesystem::path root = GetSourceRoot(source);
	const std::string virtualRoot = GetSourceVirtualRoot(source);

	if (directoryVirtualPath == virtualRoot) {
		return root;
	}

	const std::string prefix = virtualRoot + "/";
	if (directoryVirtualPath.rfind(prefix, 0) != 0) {
		return {};
	}

	const std::filesystem::path relative = directoryVirtualPath.substr(prefix.size());
	if (!IsSafeRelativePath(relative)) {
		return {};
	}
	return (root / relative).lexically_normal();
}

const char* Engine::ProjectAssetFileUtility::GetSourceVirtualRoot(ProjectAssetSource source) {

	switch (source) {
	case ProjectAssetSource::Engine:
		return "Engine/Assets";
	case ProjectAssetSource::Game:
		return "GameAssets";
	}
	return "Engine/Assets";
}

const char* Engine::ProjectAssetFileUtility::GetFileSuffix(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Text:
		return ".txt";
	case ProjectAssetFileKind::Script:
		return ".cs";
	case ProjectAssetFileKind::Scene:
		return ".scene.json";
	case ProjectAssetFileKind::Prefab:
		return ".prefab.json";
	case ProjectAssetFileKind::Material:
		return ".material.json";
	case ProjectAssetFileKind::Animation:
		return ".anim.json";
	case ProjectAssetFileKind::Shader:
		return ".shader.json";
	case ProjectAssetFileKind::RenderPipeline:
		return ".pipeline.json";
	case ProjectAssetFileKind::Folder:
	default:
		break;
	}
	return "";
}

std::string Engine::ProjectAssetFileUtility::BuildFileContent(ProjectAssetFileKind kind, const std::string& assetName) {

	switch (kind) {
	case ProjectAssetFileKind::Script:
	{
		const std::string rootNamespace = LoadGameScriptRootNamespace();
		const std::string className = MakeCSharpClassName(assetName);
		return std::format(
			"using NEMEngine;\n\n"
			"namespace {};\n\n"
			"public sealed class {} : ScriptBehaviour\n"
			"{{\n"
			"\tpublic override void Start()\n"
			"\t{{\n"
			"\t}}\n\n"
			"\tpublic override void Update()\n"
			"\t{{\n"
			"\t}}\n"
			"}}\n",
			rootNamespace,
			className);
	}
	case ProjectAssetFileKind::Scene:
		return std::format(
			"{{\n"
			"  \"Header\": {{\n"
			"    \"guid\": \"\",\n"
			"    \"name\": \"{}\",\n"
			"    \"subScenes\": [],\n"
			"    \"renderTargets\": [],\n"
			"    \"passOrder\": []\n"
			"  }},\n"
			"  \"Entities\": []\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::Prefab:
		return std::format(
			"{{\n"
			"  \"Header\": {{\n"
			"    \"guid\": \"\",\n"
			"    \"name\": \"{}\",\n"
			"    \"rootLocalFileID\": \"\",\n"
			"    \"version\": 1\n"
			"  }},\n"
			"  \"Entities\": []\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::Material:
		return std::format(
			"{{\n"
			"  \"name\": \"{}\",\n"
			"  \"domain\": \"Surface\",\n"
			"  \"passes\": [\n"
			"    {{\n"
			"      \"passName\": \"ZPrepass\",\n"
			"      \"pipeline\": \"Engine/Assets/Pipelines/Builtin/Mesh/defaultMeshZPrepass.pipeline.json\",\n"
			"      \"preferredVariant\": \"GraphicsMesh\"\n"
			"    }},\n"
			"    {{\n"
			"      \"passName\": \"Draw\",\n"
			"      \"pipeline\": \"Engine/Assets/Pipelines/Builtin/Mesh/defaultMesh.pipeline.json\",\n"
			"      \"preferredVariant\": \"GraphicsMesh\"\n"
			"    }}\n"
			"  ],\n"
			"  \"parameters\": {{\n"
			"    \"BaseColor\": {{ \"r\": 1.0, \"g\": 1.0, \"b\": 1.0, \"a\": 1.0 }},\n"
			"    \"Metallic\": 0.0,\n"
			"    \"Roughness\": 0.5\n"
			"  }}\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::Animation:
		return std::format(
			"{{\n"
			"  \"guid\": \"\",\n"
			"  \"name\": \"{}\",\n"
			"  \"duration\": 1.0,\n"
			"  \"curveTracks\": [],\n"
			"  \"eventTracks\": []\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::Shader:
		return std::format(
			"{{\n"
			"  \"name\": \"{}\",\n"
			"  \"stages\": []\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::RenderPipeline:
		return std::format(
			"{{\n"
			"  \"name\": \"{}\",\n"
			"  \"variants\": []\n"
			"}}\n",
			assetName);
	case ProjectAssetFileKind::Text:
		return "";
	case ProjectAssetFileKind::Folder:
	default:
		break;
	}
	return "";
}
