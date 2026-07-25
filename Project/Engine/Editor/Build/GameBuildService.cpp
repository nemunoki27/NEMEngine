#include "GameBuildService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace {

	// 製品名として使用できないWindows予約名か
	bool IsReservedWindowsName(const std::string& name) {

		const std::string lower = Engine::Algorithm::ToLower(name);
		const size_t extension = lower.find('.');
		const std::string baseName = lower.substr(0, extension);
		static const std::array<const char*, 22> kReserved = {
			"con", "prn", "aux", "nul",
			"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
			"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
		};
		return std::find(kReserved.begin(), kReserved.end(), baseName) != kReserved.end();
	}

	// Exe入力から拡張子を除いた製品名を取得
	bool ResolveProductName(const std::string& input, std::string& outName, std::string& outError) {

		outName = input;
		if (Engine::Algorithm::EndsWith(Engine::Algorithm::ToLower(outName), ".exe")) {
			outName.resize(outName.size() - 4);
		}
		if (outName.empty()) {
			outError = "Exeの名前を入力してください";
			return false;
		}
		if (outName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
			outName.back() == '.' || outName.back() == ' ') {
			outError = "Exeの名前に使用できない文字が含まれています";
			return false;
		}
		if (IsReservedWindowsName(outName)) {
			outError = "Windowsの予約名はExeの名前に使用できません";
			return false;
		}
		return true;
	}

	// JSONファイルを例外なしで読み込む
	nlohmann::json LoadJson(const std::filesystem::path& path) {

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) {
			return {};
		}
		const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return nlohmann::json::parse(content, nullptr, false);
	}

	// パスがprefixから始まるか
	bool StartsWith(const std::string& text, const char* prefix) {

		return text.rfind(prefix, 0) == 0;
	}

	// ゲームの生成物を持つワークスペースルートを取得
	std::filesystem::path ResolveGameBuildRoot(const std::filesystem::path& gameRoot) {

		std::error_code ec;
		for (std::filesystem::path current = gameRoot; !current.empty(); current = current.parent_path()) {

			if (std::filesystem::is_regular_file(current / "Premake/premake5.lua", ec)) {
				return current;
			}
			ec.clear();
			if (current == current.parent_path()) {
				break;
			}
		}
		return Engine::RuntimePaths::GetProjectRoot().parent_path();
	}

	// エンジンソースとSDKの両方から製品ビルドスクリプトを探索
	std::filesystem::path ResolveGameBuildScript(const std::filesystem::path& buildRoot) {

		const std::filesystem::path& engineProjectRoot = Engine::RuntimePaths::GetEngineProjectRoot();
		const std::array candidates = {
			buildRoot / "Tools/BuildGame.ps1",
			engineProjectRoot / "Tools/BuildGame.ps1",
			engineProjectRoot.parent_path() / "Tools/BuildGame.ps1",
		};
		std::error_code ec;
		for (const std::filesystem::path& candidate : candidates) {

			if (std::filesystem::is_regular_file(candidate, ec)) {
				return candidate;
			}
			ec.clear();
		}
		return engineProjectRoot / "Tools/BuildGame.ps1";
	}

	// 製品へ含めないエディター専用アセットか
	bool IsEditorOnlyAsset(const std::string& assetPath) {

		return StartsWith(assetPath, "Engine/Assets/Textures/Editor/") ||
			StartsWith(assetPath, "Engine/Assets/Shaders/Builtin/Editor/") ||
			StartsWith(assetPath, "Engine/Assets/Config/editor") ||
			StartsWith(assetPath, "Engine/Assets/Config/inspector") ||
			StartsWith(assetPath, "Engine/Assets/Config/projectPanel") ||
			StartsWith(assetPath, "Engine/Assets/Config/viewportPanel") ||
			StartsWith(assetPath, "Engine/Assets/Config/initExeData");
	}

	// 製品実行では使用しないGameAssets内の編集用ファイルか
	bool IsGameEditorOnlyAsset(const std::string& assetPath) {

		std::string lower = Engine::Algorithm::ToLower(assetPath);
		if (!StartsWith(lower, "gameassets/")) {
			return false;
	}
		if (StartsWith(lower, "gameassets/fonts/charset/")) {
			return true;
	}
		if (Engine::Algorithm::EndsWith(lower, ".meta")) {
			lower.resize(lower.size() - 5);
		}
		return Engine::Algorithm::EndsWith(lower, ".cs") ||
			Engine::Algorithm::EndsWith(lower, ".ttf") ||
			Engine::Algorithm::EndsWith(lower, ".otf");
	}

	// 製品へ配置する1ファイル
	struct BuildFileEntry {

		std::filesystem::path source;
		std::string destination;
	};

	// シーン内で使用される描画機能
	struct BuildUsage {

		bool mesh = false;
		bool sprite = false;
		bool text = false;
		bool line = false;
		bool fillMesh = false;
		bool primitive = false;
		bool primitive2D = false;
		bool progress = false;
		bool particle = false;
	};

	// GameAssets全体と参照されるEngineアセットを収集する
	class GameBuildAssetCollector {
	public:

		explicit GameBuildAssetCollector(const Engine::AssetDatabase& database) :
			database_(database) {
		}

		// GameAssets全体を起点に依存ファイルを収集
		bool Collect(Engine::AssetID startupScene, std::vector<BuildFileEntry>& outFiles, std::string& outError) {

			AddAllGameAssets();
			AddAsset(startupScene);
			AddAsset(Engine::BuiltinAssets::Materials::ToneMapToView);
			AddAsset(Engine::BuiltinAssets::Materials::FullscreenCopy);
			AddAsset(Engine::BuiltinAssets::Materials::RaytracingReflection);
			AddAsset(Engine::BuiltinAssets::Materials::IrisTransition);

			AddFixedRuntimeFiles();
			ProcessAssets();
			AddUsageAssets();
			ProcessAssets();

			if (!errors_.empty()) {
				outError = errors_.front();
				return false;
			}

			outFiles.reserve(files_.size());
			for (const auto& [destination, source] : files_) {
				outFiles.push_back({ source, destination });
			}
			return true;
		}
	private:

		// AssetIDを処理待ちへ追加
		void AddAsset(Engine::AssetID assetID) {

			if (!assetID || !queuedAssets_.insert(assetID).second) {
				return;
			}
			assetQueue_.push_back(assetID);
		}

		// 物理ファイルを配置一覧へ追加
		void AddFile(const std::filesystem::path& source, const std::string& destination) {

			std::error_code ec;
			if (source.empty() || destination.empty() ||
				!std::filesystem::is_regular_file(source, ec) || ec) {
				return;
			}
			if (IsEditorOnlyAsset(destination) || IsGameEditorOnlyAsset(destination)) {
				return;
			}
			files_.emplace(destination, source);
		}

		// アセット本体と.metaを配置一覧へ追加
		void AddAssetFile(const Engine::AssetMeta& meta) {

			if (meta.type == Engine::AssetType::Script ||
				IsEditorOnlyAsset(meta.assetPath) || IsGameEditorOnlyAsset(meta.assetPath)) {
				return;
			}

			const std::filesystem::path source = database_.ResolveFullPath(meta.guid);
			AddFile(source, meta.assetPath);

			std::filesystem::path metaPath = source;
			metaPath += L".meta";
			AddFile(metaPath, meta.assetPath + ".meta");
		}

		// 論理アセットパスのファイルと.metaを追加
		void AddLogicalFile(const std::string& assetPath) {

			if (const Engine::AssetMeta* meta = database_.FindByPath(assetPath)) {
				AddAsset(meta->guid);
				return;
			}
			const std::filesystem::path source = Engine::RuntimePaths::ResolveAssetPath(assetPath);
			AddFile(source, assetPath);
			std::filesystem::path metaPath = source;
			metaPath += L".meta";
			AddFile(metaPath, assetPath + ".meta");
		}

		// GameAssets内の全ファイルを追加しアセットは依存解析へ回す
		void AddAllGameAssets() {

			const std::filesystem::path gameRoot = Engine::RuntimePaths::GetGameRoot();
			const std::filesystem::path gameAssetsRoot = gameRoot / "GameAssets";
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(gameAssetsRoot, ec), end;
				it != end && !ec; it.increment(ec)) {

				if (!it->is_regular_file(ec)) {
					continue;
				}

				const std::filesystem::path relative = std::filesystem::relative(it->path(), gameRoot, ec);
				if (ec) {
					break;
				}
				const std::string assetPath = Engine::Algorithm::ConvertString(relative.generic_wstring());
				AddFile(it->path(), assetPath);

				if (Engine::Algorithm::EndsWith(Engine::Algorithm::ToLower(assetPath), ".meta")) {
					continue;
				}
				if (const Engine::AssetMeta* meta = database_.FindByPath(assetPath)) {
					AddAsset(meta->guid);
				}
			}
			if (ec) {
				errors_.push_back("GameAssetsのファイルを収集できません: " + ec.message());
			}
		}

		// AssetIDの依存関係と付属ファイルを処理
		void ProcessAssets() {

			while (!assetQueue_.empty()) {

				const Engine::AssetID assetID = assetQueue_.front();
				assetQueue_.pop_front();

				const Engine::AssetMeta* meta = database_.Find(assetID);
				if (!meta) {
					Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
						"[GameBuild] referenced asset was not found and will be skipped. guid={}",
						Engine::ToString(assetID));
					continue;
				}
				if (meta->type == Engine::AssetType::Script ||
					IsEditorOnlyAsset(meta->assetPath) || IsGameEditorOnlyAsset(meta->assetPath)) {
					continue;
				}

				AddAssetFile(*meta);
				for (const Engine::AssetID dependency : database_.FindDependencies(assetID)) {
					AddAsset(dependency);
				}

				const std::filesystem::path source = database_.ResolveFullPath(assetID);
				InspectFile(*meta, source);
			}
		}

		// JSON参照やシェーダーincludeやモデル付属ファイルを調べる
		void InspectFile(const Engine::AssetMeta& meta, const std::filesystem::path& source) {

			const std::string extension = Engine::Algorithm::ToLower(source.extension().string());
			if (extension == ".json" || extension == ".effect" || extension == ".prefab" || extension == ".scene") {

				const nlohmann::json data = LoadJson(source);
				if (!data.is_discarded() && !data.is_null()) {
					InspectJson(data);
				}
			}
			if (extension == ".hlsl" || extension == ".hlsli") {
				CollectShaderIncludes(source);
			}
			if (meta.type == Engine::AssetType::Mesh) {
				CollectModelSidecars(source);
			}
			if (meta.type == Engine::AssetType::ParticleEffect) {
				usage_.particle = true;
			}
		}

		// JSON内のアセット参照と使用コンポーネントを再帰収集
		void InspectJson(const nlohmann::json& node) {

			if (node.is_object()) {

				for (auto it = node.begin(); it != node.end(); ++it) {

					const std::string& key = it.key();
					if (key == "MeshRenderer") usage_.mesh = true;
					else if (key == "SpriteRenderer") usage_.sprite = true;
					else if (key == "TextRenderer") usage_.text = true;
					else if (key == "LineRenderer") usage_.line = true;
					else if (key == "FillMeshRenderer") usage_.fillMesh = true;
					else if (key == "PrimitiveRenderer") {
						usage_.primitive = true;
						usage_.primitive2D = true;
					}
					else if (key == "UIProgress") {
						usage_.primitive2D = true;
						usage_.progress = true;
					} else if (key == "EffectEmitter") {
						usage_.particle = true;
					}

					if (it->is_string()) {

						const std::string value = it->get<std::string>();
						if (const std::optional<Engine::AssetID> parsed = Engine::TryParseUUID16Hex(value)) {
							if (database_.Find(*parsed)) {
								AddAsset(*parsed);
							}
						} else if (key == "file" &&
							(StartsWith(value, "Engine/Assets/") || StartsWith(value, "GameAssets/"))) {
							AddLogicalFile(value);
						}
					}
					InspectJson(*it);
				}
			} else if (node.is_array()) {

				for (const nlohmann::json& element : node) {
					InspectJson(element);
				}
			}
		}

		// HLSLの#includeをDXCと同じ探索順で再帰収集
		void CollectShaderIncludes(const std::filesystem::path& shaderPath) {

			std::error_code ec;
			const std::filesystem::path normalized = std::filesystem::weakly_canonical(shaderPath, ec);
			const std::string key = Engine::Algorithm::ConvertString(
				(ec ? shaderPath.lexically_normal() : normalized).generic_wstring());
			if (!scannedShaderFiles_.insert(key).second) {
				return;
			}

			std::ifstream file(shaderPath);
			if (!file.is_open()) {
				return;
			}

			static const std::regex kIncludePattern(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
			std::string line;
			while (std::getline(file, line)) {

				std::smatch match;
				if (!std::regex_search(line, match, kIncludePattern) || match.size() < 2) {
					continue;
				}

				const std::filesystem::path includeName = match[1].str();
				const std::array<std::filesystem::path, 3> candidates = {
					shaderPath.parent_path() / includeName,
					Engine::RuntimePaths::GetEngineAssetPath("Shaders") / includeName,
					Engine::RuntimePaths::GetGameRoot() / "GameAssets/Shaders" / includeName,
				};
				for (const std::filesystem::path& candidate : candidates) {

					if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
						ec.clear();
						continue;
					}
					const std::string assetPath = Engine::RuntimePaths::ToAssetPath(candidate);
					AddLogicalFile(assetPath);
					CollectShaderIncludes(candidate);
					break;
				}
			}
		}

		// glTFとOBJが外部参照するファイルを収集
		void CollectModelSidecars(const std::filesystem::path& modelPath) {

			const std::string extension = Engine::Algorithm::ToLower(modelPath.extension().string());
			if (extension == ".gltf") {

				const nlohmann::json data = LoadJson(modelPath);
				CollectModelUris(data, modelPath.parent_path());
			} else if (extension == ".obj") {

				CollectObjSidecars(modelPath);
			}
		}

		// glTF内の外部URIを再帰収集
		void CollectModelUris(const nlohmann::json& node, const std::filesystem::path& modelDirectory) {

			if (node.is_object()) {

				for (auto it = node.begin(); it != node.end(); ++it) {

					if (it.key() == "uri" && it->is_string()) {

						const std::string uri = it->get<std::string>();
						if (!StartsWith(uri, "data:")) {
							AddModelSidecar(modelDirectory / Engine::Algorithm::PathFromUTF8(uri));
						}
					}
					CollectModelUris(*it, modelDirectory);
				}
			} else if (node.is_array()) {

				for (const nlohmann::json& element : node) {
					CollectModelUris(element, modelDirectory);
				}
			}
		}

		// OBJのmtllibとMTL内のテクスチャ参照を収集
		void CollectObjSidecars(const std::filesystem::path& modelPath) {

			std::ifstream file(modelPath);
			if (!file.is_open()) {
				return;
			}

			std::string line;
			while (std::getline(file, line)) {

				std::istringstream stream(line);
				std::string command;
				stream >> command;
				if (command != "mtllib") {
					continue;
				}

				std::string relative;
				std::getline(stream >> std::ws, relative);
				const std::filesystem::path materialPath =
					modelPath.parent_path() / Engine::Algorithm::PathFromUTF8(relative);
				AddModelSidecar(materialPath);
				CollectMtlTextures(materialPath);
			}
		}

		// MTL内のmap系テクスチャ参照を収集
		void CollectMtlTextures(const std::filesystem::path& materialPath) {

			std::ifstream file(materialPath);
			if (!file.is_open()) {
				return;
			}

			std::string line;
			while (std::getline(file, line)) {

				std::istringstream stream(line);
				std::string command;
				stream >> command;
				const std::string lower = Engine::Algorithm::ToLower(command);
				if (!StartsWith(lower, "map_") && lower != "bump" && lower != "disp" && lower != "decal") {
					continue;
				}

				std::string relative;
				std::getline(stream >> std::ws, relative);
				AddModelSidecar(materialPath.parent_path() / Engine::Algorithm::PathFromUTF8(relative));
			}
		}

		// モデル付属ファイルを論理パスを維持して追加
		void AddModelSidecar(const std::filesystem::path& source) {

			const std::string assetPath = Engine::RuntimePaths::ToAssetPath(source);
			if (assetPath.empty()) {
				return;
			}
			AddLogicalFile(assetPath);
		}

		// ウィンドウ設定やグローバル設定や直接参照シェーダーを追加
		void AddFixedRuntimeFiles() {

			AddLogicalFile("Engine/Assets/Window/windowSetting.json");
			AddLogicalFile("Engine/Assets/Shaders/Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl");
			AddLogicalFile("Engine/Assets/Shaders/Builtin/Lighting/deferredLighting.PS.hlsl");
			AddLogicalFile("Engine/Assets/Shaders/Builtin/Lighting/skyboxIrradiance.CS.hlsl");

			const std::filesystem::path gameRoot = Engine::RuntimePaths::GetGameRoot();
			const std::array<const char*, 3> gameProjectSettings = {
				"InputActions.json",
				"ScriptExecutionOrder.json",
				"TagSettings.json",
			};
			for (const char* setting : gameProjectSettings) {

				const std::string destination = std::string("ProjectSettings/") + setting;
				AddFile(gameRoot / destination, destination);
			}

			const std::array<const char*, 3> runtimeConfigs = {
				Engine::ConfigPaths::kInputDevice,
				Engine::ConfigPaths::kGraphicsFeatureSettings,
				Engine::ConfigPaths::kFrameRate,
			};
			for (const char* config : runtimeConfigs) {
				AddFile(gameRoot / config, config);
			}
		}

		// 使用コンポーネントに対応する既定アセットを追加
		void AddUsageAssets() {

			const Engine::DefaultMaterialSettings& defaults = Engine::DefaultMaterialSettings::GetInstance();
			if (usage_.mesh) {
				AddAsset(defaults.GetMeshOrBuiltin());
				AddAsset(Engine::BuiltinAssets::Pipelines::Skinning);
				AddAsset(Engine::BuiltinAssets::Pipelines::BuildIndexedIndirectArgs);
			}
			if (usage_.sprite) AddAsset(defaults.GetSpriteOrBuiltin());
			if (usage_.text) AddAsset(defaults.GetTextOrBuiltin());
			if (usage_.line) AddAsset(defaults.GetLineOrBuiltin());
			if (usage_.fillMesh) AddAsset(defaults.GetFillMeshOrBuiltin());
			if (usage_.primitive) AddAsset(defaults.GetPrimitiveOrBuiltin());
			if (usage_.primitive2D) AddAsset(defaults.GetPrimitive2DOrBuiltin());
			if (usage_.progress) AddAsset(Engine::BuiltinAssets::Materials::ProgressPrimitive);
			if (usage_.particle) {
				AddAsset(Engine::BuiltinAssets::Effects::DefaultParticle);
				AddAsset(Engine::BuiltinAssets::Materials::DefaultParticle);
				AddAsset(Engine::BuiltinAssets::Materials::DefaultParticle2D);
				AddAsset(Engine::BuiltinAssets::Pipelines::ParticleTrail);
				AddAsset(Engine::BuiltinAssets::Pipelines::ParticleRingMS);
				AddAsset(Engine::BuiltinAssets::Pipelines::ParticleCylinderMS);
			}
		}

		const Engine::AssetDatabase& database_;
		std::deque<Engine::AssetID> assetQueue_;
		std::unordered_set<Engine::AssetID> queuedAssets_;
		std::unordered_set<std::string> scannedShaderFiles_;
		std::map<std::string, std::filesystem::path> files_;
		std::vector<std::string> errors_;
		BuildUsage usage_{};
	};

	// PowerShellへ渡す引数を二重引用符で囲む
	std::wstring QuoteArgument(const std::filesystem::path& path) {

		return L"\"" + path.wstring() + L"\"";
	}
}

//============================================================================
//	GameBuildService classMethods
//============================================================================
Engine::GameBuildService::~GameBuildService() {

	processRunner_.Terminate();
	RemoveManifest();
}

void Engine::GameBuildService::RefreshScenes(const AssetDatabase& database) {

	scenes_.clear();
	const std::filesystem::path gameAssetsRoot = RuntimePaths::GetGameRoot() / "GameAssets";

	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(gameAssetsRoot, ec), end;
		it != end && !ec; it.increment(ec)) {

		if (!it->is_regular_file(ec) || AssetTypeResolver::GuessByPath(it->path()) != AssetType::Scene) {
			continue;
		}
		const std::string assetPath = RuntimePaths::ToAssetPath(it->path());
		const AssetMeta* meta = database.FindByPath(assetPath);
		if (!meta || meta->type != AssetType::Scene || !StartsWith(meta->assetPath, "GameAssets/")) {
			continue;
		}

		std::filesystem::path displayPath = std::filesystem::relative(it->path(), gameAssetsRoot, ec);
		if (ec) {
			ec.clear();
			displayPath = it->path().filename();
		}
		scenes_.push_back({
			meta->guid,
			meta->assetPath,
			Algorithm::ConvertString(displayPath.generic_wstring())
			});
	}

	std::sort(scenes_.begin(), scenes_.end(), [](const GameBuildSceneEntry& lhs, const GameBuildSceneEntry& rhs) {
		return lhs.displayName < rhs.displayName;
		});
}

bool Engine::GameBuildService::Start(const GameBuildSettings& settings,
	const AssetDatabase& database, std::string& outError) {

	if (IsBuilding()) {
		outError = "ビルドは既に実行中です";
		return false;
	}

	RemoveManifest();
	std::filesystem::path scriptPath;
	if (!WriteManifest(settings, database, scriptPath, outError)) {
		RemoveManifest();
		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		failureDetail_ = outError;
		return false;
	}

	std::wstring commandLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
	commandLine += QuoteArgument(scriptPath);
	commandLine += L" -ManifestPath ";
	commandLine += QuoteArgument(manifestPath_);

	const std::filesystem::path buildRoot = ResolveGameBuildRoot(RuntimePaths::GetGameRoot());
	if (!processRunner_.Start(commandLine, buildRoot)) {
		RemoveManifest();
		outError = "製品ビルドプロセスを開始できませんでした";
		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		failureDetail_ = outError;
		return false;
	}

	state_ = GameBuildState::Building;
	statusMessage_ = "ビルド中...";
	failureDetail_.clear();
	Logger::Output(LogType::Engine, "[GameBuild] started. output={}",
		Algorithm::PathToUTF8(outputDirectory_));
	return true;
}

void Engine::GameBuildService::Update() {

	if (!IsBuilding()) {
		return;
	}

	const bool finished = processRunner_.Poll([this](const std::string& line) {

		if (line.empty()) {
			return;
		}
		Logger::Output(LogType::Engine, "[GameBuild] {}", line);
		if (line.rfind("[NEM_GAME_BUILD_ERROR]", 0) == 0) {
			failureDetail_ = line.substr(std::string("[NEM_GAME_BUILD_ERROR]").size());
			while (!failureDetail_.empty() && failureDetail_.front() == ' ') {
				failureDetail_.erase(failureDetail_.begin());
			}
		}
		});
	if (!finished) {
		return;
	}

	const int32_t exitCode = processRunner_.ExitCode();
	RemoveManifest();
	if (exitCode == 0) {

		state_ = GameBuildState::Completed;
		statusMessage_ = "完了しました";
		Logger::Output(LogType::Engine, "[GameBuild] completed. output={}",
			Algorithm::PathToUTF8(outputDirectory_));
	} else {

		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		if (failureDetail_.empty()) {
			failureDetail_ = "製品ビルドに失敗しました、engine.logを確認してください";
		}
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[GameBuild] failed. exitCode={} detail={}", exitCode, failureDetail_);
	}
}

void Engine::GameBuildService::ResetStatus() {

	if (IsBuilding()) {
		return;
	}
	state_ = GameBuildState::Idle;
	statusMessage_.clear();
	failureDetail_.clear();
}

bool Engine::GameBuildService::WriteManifest(const GameBuildSettings& settings,
	const AssetDatabase& database, std::filesystem::path& outScriptPath, std::string& outError) {

	const AssetMeta* sceneMeta = database.Find(settings.startupScene);
	if (!sceneMeta || sceneMeta->type != AssetType::Scene ||
		!StartsWith(sceneMeta->assetPath, "GameAssets/")) {
		outError = "GameAssets内の最初のシーンを選択してください";
		return false;
	}

	std::string productName;
	if (!ResolveProductName(settings.executableName, productName, outError)) {
		return false;
	}

	std::error_code ec;
	if (settings.outputRoot.empty()) {
		outError = "出力先を選択してください";
		return false;
	}
	std::filesystem::create_directories(settings.outputRoot, ec);
	if (ec || !std::filesystem::is_directory(settings.outputRoot, ec)) {
		outError = "出力先フォルダを作成できません";
		return false;
	}

	const std::filesystem::path gameRoot = RuntimePaths::GetGameRoot();
	const std::filesystem::path buildRoot = ResolveGameBuildRoot(gameRoot);
	const std::filesystem::path projectNamePath = gameRoot.filename();
	const std::string projectName = Algorithm::PathToUTF8(projectNamePath);
	std::filesystem::path projectFileName = projectNamePath;
	projectFileName += L".vcxproj";
	const std::filesystem::path projectPath = gameRoot / projectFileName;
	const std::filesystem::path sourceRuntime =
		buildRoot / "Generated/Output/Release" / projectNamePath;
	outScriptPath = ResolveGameBuildScript(buildRoot);

	if (!std::filesystem::is_regular_file(projectPath, ec)) {
		outError = "ゲームプロジェクトが見つかりません: " + Algorithm::PathToUTF8(projectPath);
		return false;
	}
	if (!std::filesystem::is_regular_file(outScriptPath, ec)) {
		outError = "製品ビルドスクリプトが見つかりません: " + Algorithm::PathToUTF8(outScriptPath);
		return false;
	}

	std::vector<BuildFileEntry> files;
	GameBuildAssetCollector collector(database);
	if (!collector.Collect(settings.startupScene, files, outError)) {
		return false;
	}

	nlohmann::json manifest = nlohmann::json::object();
	manifest["projectPath"] = Algorithm::ConvertString(projectPath.generic_wstring());
	manifest["sourceRuntime"] = Algorithm::ConvertString(sourceRuntime.generic_wstring());
	manifest["runtimeExecutable"] = projectName + ".exe";
	manifest["outputRoot"] = Algorithm::ConvertString(settings.outputRoot.generic_wstring());
	manifest["productName"] = productName;
	manifest["executableName"] = productName + ".exe";
	manifest["startupScene"] = ToString(settings.startupScene);
	manifest["startupFullscreen"] = settings.startupFullscreen;
	manifest["files"] = nlohmann::json::array();
	for (const BuildFileEntry& file : files) {
		manifest["files"].push_back({
			{ "source", Algorithm::ConvertString(file.source.generic_wstring()) },
			{ "destination", file.destination },
			});
	}

	const std::filesystem::path manifestDirectory = buildRoot / "Generated/GameBuild";
	std::filesystem::create_directories(manifestDirectory, ec);
	if (ec) {
		outError = "ビルド用一時フォルダを作成できません";
		return false;
	}
	std::filesystem::path manifestFileName = projectNamePath;
	manifestFileName += L".gameBuildManifest.json";
	manifestPath_ = manifestDirectory / manifestFileName;
	std::ofstream file(manifestPath_, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		outError = "ビルド用マニフェストを作成できません";
		return false;
	}
	file << manifest.dump(2);
	if (!file.good()) {
		outError = "ビルド用マニフェストを書き込めません";
		return false;
	}

	outputDirectory_ = settings.outputRoot / Algorithm::PathFromUTF8(productName);
	return true;
}

void Engine::GameBuildService::RemoveManifest() {

	if (manifestPath_.empty()) {
		return;
	}
	std::error_code ec;
	std::filesystem::remove(manifestPath_, ec);
	manifestPath_.clear();
}
