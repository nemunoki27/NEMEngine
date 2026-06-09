#include "ManagedScriptBuildService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// windows（_wcsicmp等）
#include <cwctype>
// c++
#include <algorithm>
#include <cctype>
#include <vector>

namespace {

	// GameScripts のアセンブリ/付随ファイル名
	constexpr const wchar_t* kAssemblyFileName = L"GameScripts.dll";
	// Script Manifest（Stable Script Type GUID の一覧。load前に検証する build artifact）
	constexpr const wchar_t* kManifestFileName = L"GameScripts.scriptmanifest.json";

	std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
	}

	std::wstring Widen(const std::string& text) {
		return Engine::Algorithm::ConvertString(text);
	}

	// 現在のビルド構成名（Debug/Develop/Release）
	std::string BuildProfile() {
		return _PROFILE;
	}

	// 監視対象から外すディレクトリ名（大小無視）。生成物・VCS・reload作業領域を含む
	bool IsExcludedDirectory(const std::wstring& directoryName) {
		static const wchar_t* kExcluded[] = {
			L"bin", L"obj", L".git", L".vs", L"Generated", L"Library", L"Temp",
			L"Staging", L"Shadow", L"LastKnownGood",
		};
		for (const wchar_t* excluded : kExcluded) {
			if (_wcsicmp(directoryName.c_str(), excluded) == 0) {
				return true;
			}
		}
		return false;
	}

	// 生成された .cs（*.g.cs / *.generated.cs）か
	bool IsGeneratedScriptFile(const std::wstring& fileName) {
		const auto endsWith = [&fileName](const wchar_t* suffix) {
			const size_t suffixLength = std::wcslen(suffix);
			if (fileName.size() < suffixLength) {
				return false;
			}
			return _wcsicmp(fileName.c_str() + (fileName.size() - suffixLength), suffix) == 0;
			};
		return endsWith(L".g.cs") || endsWith(L".generated.cs");
	}

	// 状態名（ログ用）
	const char* StateName(Engine::ManagedScriptBuildService::State state) {
		using State = Engine::ManagedScriptBuildService::State;
		switch (state) {
		case State::Idle: return "Idle";
		case State::Debouncing: return "Debouncing";
		case State::Building: return "Building";
		case State::BuildSucceeded: return "BuildSucceeded";
		case State::BuildFailed: return "BuildFailed";
		case State::Staging: return "Staging";
		case State::ReloadPending: return "ReloadPending";
		case State::Reloading: return "Reloading";
		case State::ReloadSucceeded: return "ReloadSucceeded";
		case State::ReloadFailed: return "ReloadFailed";
		case State::FallbackLoading: return "FallbackLoading";
		case State::FallbackSucceeded: return "FallbackSucceeded";
		case State::FallbackFailed: return "FallbackFailed";
		}
		return "Unknown";
	}

	double DurationMs(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end) {
		return std::chrono::duration<double, std::milli>(end - begin).count();
	}

	// build 出力行が compiler/MSBuild error を含むか（大小無視で " error " / "error CS" 等を拾う）
	bool ContainsErrorToken(const std::string& line) {

		std::string lower;
		lower.reserve(line.size());
		for (char c : line) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		// 集計行 "N Error(s)" は実エラー本文ではないので除外し、実際の error 行だけを対象にする
		if (lower.find("error(s)") != std::string::npos) {
			return false;
		}
		return lower.find("error") != std::string::npos;
	}
}

//============================================================================
//	ManagedScriptBuildService classMethods
//============================================================================
Engine::ManagedScriptBuildService::~ManagedScriptBuildService() {
	Shutdown();
}

void Engine::ManagedScriptBuildService::Initialize(ManagedScriptRuntime* runtime) {

	runtime_ = runtime;
	state_ = State::Idle;
	dirty_ = false;
	hasSnapshot_ = false;
	sourceSnapshot_.clear();
	nextScanTime_ = std::chrono::steady_clock::time_point{};
	playBuildRequested_ = false;
	playBuildResult_ = PlayBuildResult::Succeeded;

	// 起動時に source baseline を取得する（初回は変更扱いにしない）
	PollSourceChanges();
	dirty_ = false;

	// 現在ロード中の正常DLLを last-known-good として確保しておく（初回 reload 失敗時の復旧用）
	SeedLastKnownGood();
}

void Engine::ManagedScriptBuildService::Shutdown() {

	process_.Terminate();
	state_ = State::Idle;
	runtime_ = nullptr;
}

void Engine::ManagedScriptBuildService::Tick(bool playing) {

	if (!runtime_ || !runtime_->IsInitialized()) {
		return;
	}

	// 変更検知（Play中も dirty は記録する。reload はしない）
	PollSourceChanges();

	// Play中に変更があったら「Stop後に反映」を一度だけ通知する
	if (playing) {
		if (dirty_ && !playDirtyNotified_) {

			Logger::Output(LogType::GameLogic, spdlog::level::info,
				"GameScripts: C# changes detected during Play. They will be rebuilt/reloaded after Stop.");
			playDirtyNotified_ = true;
		}
	} else {
		playDirtyNotified_ = false;
	}

	// 状態機械を進める
	AdvanceState(playing);
}

void Engine::ManagedScriptBuildService::RequestPlayBuild() {

	playBuildRequested_ = true;
	playBuildResult_ = PlayBuildResult::Pending;

	// ビルド対象が無ければ即成功（managed scriptなしでPlay可能）
	if (!runtime_ || runtime_->GameScriptProjectPath().empty()) {
		playBuildResult_ = PlayBuildResult::Succeeded;
	}
}

void Engine::ManagedScriptBuildService::PollSourceChanges() {

	const auto now = std::chrono::steady_clock::now();
	if (now < nextScanTime_) {
		return;
	}
	nextScanTime_ = now + scanInterval_;

	const std::filesystem::path projectPath = runtime_->GameScriptProjectPath();
	if (projectPath.empty()) {
		sourceSnapshot_.clear();
		hasSnapshot_ = false;
		return;
	}

	// csproj 単体 + Scripts ルート + GameAssets ルートを集約する
	std::unordered_map<std::string, SourceStamp> current;
	const auto addFile = [&current](const std::filesystem::path& path) {
		std::error_code timeError{};
		std::error_code sizeError{};
		const auto time = std::filesystem::last_write_time(path, timeError);
		const auto size = std::filesystem::file_size(path, sizeError);
		if (!timeError && !sizeError) {
			current.emplace(ToUtf8Path(path.lexically_normal()), SourceStamp{ time, size });
		}
		};

	std::error_code projectExists{};
	if (std::filesystem::exists(projectPath, projectExists) && !projectExists) {
		addFile(projectPath);
	}

	const std::filesystem::path scriptsRoot = projectPath.parent_path();
	const std::filesystem::path gameAssetsRoot = scriptsRoot.parent_path() / "GameAssets";
	for (const std::filesystem::path& root : { scriptsRoot, gameAssetsRoot }) {

		std::error_code rootExists{};
		if (!std::filesystem::exists(root, rootExists) || rootExists) {
			continue;
		}
		// permission error で監視全体を止めないよう error_code 版で走査する
		std::error_code iterateError{};
		auto iterator = std::filesystem::recursive_directory_iterator(
			root, std::filesystem::directory_options::skip_permission_denied, iterateError);
		const std::filesystem::recursive_directory_iterator end{};
		for (; iterator != end; iterator.increment(iterateError)) {

			if (iterateError) {
				iterateError.clear();
				continue;
			}
			const std::filesystem::directory_entry& entry = *iterator;
			std::error_code statusError{};
			if (entry.is_directory(statusError) && !statusError) {
				if (IsExcludedDirectory(entry.path().filename().wstring())) {
					iterator.disable_recursion_pending();
				}
				continue;
			}
			if (!entry.is_regular_file(statusError) || statusError) {
				continue;
			}
			const std::filesystem::path& filePath = entry.path();
			if (filePath.extension() != ".cs") {
				continue;
			}
			if (IsGeneratedScriptFile(filePath.filename().wstring())) {
				continue;
			}
			addFile(filePath);
		}
	}

	if (!hasSnapshot_) {
		sourceSnapshot_ = std::move(current);
		hasSnapshot_ = true;
		return;
	}

	// 変更判定（サイズ・更新時刻・追加削除）
	bool changed = current.size() != sourceSnapshot_.size();
	int32_t changedCount = 0;
	if (!changed) {
		for (const auto& [path, stamp] : current) {
			auto it = sourceSnapshot_.find(path);
			if (it == sourceSnapshot_.end() || it->second.time != stamp.time || it->second.size != stamp.size) {
				changed = true;
				++changedCount;
			}
		}
	} else {
		changedCount = static_cast<int32_t>(current.size() > sourceSnapshot_.size()
			? current.size() - sourceSnapshot_.size() : sourceSnapshot_.size() - current.size());
	}

	sourceSnapshot_ = std::move(current);
	if (changed) {

		dirty_ = true;
		lastChangeTime_ = now;
		diagnostics_.changedSourceCount = changedCount;
	}
}

void Engine::ManagedScriptBuildService::AdvanceState(bool playing) {

	switch (state_) {
	case State::Idle:
	{
		// Play 用の強制ビルドを最優先で開始する
		if (!playing && playBuildRequested_ && playBuildResult_ == PlayBuildResult::Pending) {
			StartBuild(true);
			return;
		}
		// 通常の Edit 変更は debounce へ
		if (!playing && dirty_) {
			SetState(State::Debouncing);
		}
		return;
	}
	case State::Debouncing:
	{
		// Play へ入ったら一旦保留（変更は dirty のまま、Stop 後に再開）
		if (playing || !dirty_) {
			SetState(State::Idle);
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - lastChangeTime_ >= debounce_) {
			StartBuild(false);
		}
		return;
	}
	case State::Building:
	{
		const bool finished = process_.Poll([this](const std::string& line) {
			// build 出力を Editor console（GameLogic ログ）へ逐次転送する
			Logger::Output(LogType::GameLogic, spdlog::level::info, "[GameScripts build] {}", line);
			// engine.log 要約用に error 行の最初と最後を保持する（全文は gameLogic.log 側）
			if (ContainsErrorToken(line)) {
				if (firstErrorLine_.empty()) {
					firstErrorLine_ = line;
				}
				lastErrorLine_ = line;
			}
			});
		if (finished) {
			OnBuildFinished();
		}
		return;
	}
	case State::ReloadPending:
	{
		// reload の適用は main thread かつ Edit モードでのみ行う
		if (playing) {
			return;
		}
		ApplyReload();
		return;
	}
	default:
		// 他の状態は OnBuildFinished / ApplyReload / ApplyFallback 内で同期的に遷移済み
		return;
	}
}

bool Engine::ManagedScriptBuildService::StartBuild(bool forPlay) {

	const std::filesystem::path projectPath = runtime_->GameScriptProjectPath();
	if (projectPath.empty()) {

		// ビルド対象なし
		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Succeeded;
		}
		dirty_ = false;
		SetState(State::Idle);
		return false;
	}

	// --no-dependencies で GameScripts のみをビルドするため、前提成果物が揃っているか先に確認する。
	// ScriptCore は Editor 実行中にロード済みのため runtime build では作り直さない（上書き厳禁）。
	if (!VerifyBuildPrerequisites()) {

		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		dirty_ = false;
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	// 変更は消費する（ビルド中の追加変更は PollSourceChanges が再び dirty にする）
	dirty_ = false;
	currentForPlay_ = forPlay;
	buildStartTime_ = std::chrono::steady_clock::now();
	diagnostics_ = ReloadDiagnostics{};
	diagnostics_.buildId = ++buildCounter_;
	firstErrorLine_.clear();
	lastErrorLine_.clear();

	// staging ディレクトリ作成の失敗は無視せず、絶対パスと error を出して中断する
	std::error_code dirError{};
	currentStagingDir_ = StagingRoot() / std::to_wstring(diagnostics_.buildId);
	std::filesystem::create_directories(currentStagingDir_, dirError);
	if (dirError) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: failed to create staging directory. path={} error={}",
			ToUtf8Path(currentStagingDir_), dirError.message());
		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	// staging 出力へ dotnet build（実行中DLLは触らない）。
	// -o は project reference の出力解決まで staging へ向け CS0006 を起こすため使わず、
	// GameScripts 専用の NEMScriptStagingOutput プロパティでこのプロジェクトの出力だけを staging へ向ける。
	const std::wstring command =
		L"dotnet build \"" + projectPath.wstring() + L"\" -c " + Widen(BuildProfile()) +
		L" --nologo --no-dependencies -p:DebugType=portable -p:DebugSymbols=true -p:Optimize=false" +
		L" -p:NEMScriptStagingOutput=\"" + currentStagingDir_.wstring() + L"\"";

	// build failure 時に engine.log へ要約を残すため、実行コマンドと working directory を保持する
	lastBuildCommandUtf8_ = Algorithm::ConvertString(command);
	lastBuildWorkingDir_ = projectPath.parent_path();

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: build start. buildId={} forPlay={} staging={}",
		diagnostics_.buildId, forPlay, ToUtf8Path(currentStagingDir_));
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: build command. cwd={} cmd={}",
		ToUtf8Path(lastBuildWorkingDir_), lastBuildCommandUtf8_);

	if (!process_.Start(command, lastBuildWorkingDir_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: failed to start dotnet build process. cmd={}", lastBuildCommandUtf8_);
		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	SetState(State::Building);
	return true;
}

bool Engine::ManagedScriptBuildService::VerifyBuildPrerequisites() const {

	const std::filesystem::path projectPath = runtime_ ? runtime_->GameScriptProjectPath() : std::filesystem::path{};
	if (projectPath.empty()) {
		return true;
	}

	// GameScripts.csproj は <root>/<Game>/Scripts/GameScripts.csproj。
	// reference は "..\..\Engine\Managed\..." なので engine の Managed ディレクトリをそこから導出する。
	const std::filesystem::path projectRoot = projectPath.parent_path().parent_path().parent_path();
	const std::filesystem::path engineManagedDir = projectRoot / "Engine" / "Managed";

	std::error_code existsError{};
	if (!std::filesystem::exists(engineManagedDir, existsError) || existsError) {
		// 想定外のレイアウト（テンプレート等）では誤検知を避けるため検証をスキップする
		return true;
	}

	const std::string profile = BuildProfile();

	// Roslyn analyzer（NEM.ScriptCodeGen.dll）。--no-dependencies では作られないため必須。
	const std::filesystem::path codeGenDll =
		engineManagedDir / "NEM.ScriptCodeGen" / "bin" / profile / "netstandard2.0" / "NEM.ScriptCodeGen.dll";

	// NEM.ScriptCore.dll は配置先がデプロイ構成で異なるため候補を順に確認する
	const std::filesystem::path repoRoot = projectRoot.parent_path();
	const std::filesystem::path scriptCoreCandidates[] = {
		repoRoot / "Generated" / "Managed" / "NEM.ScriptCore" / profile / "NEM.ScriptCore.dll",
		projectRoot / "Engine" / "Library" / "Managed" / profile / "NEM.ScriptCore.dll",
	};

	const auto pathExists = [](const std::filesystem::path& path) {
		std::error_code error{};
		return std::filesystem::exists(path, error) && !error;
		};

	bool ok = true;

	if (!pathExists(codeGenDll)) {

		ok = false;
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: prerequisite missing (analyzer). path={} profile={}",
			ToUtf8Path(codeGenDll), profile);
	}

	bool scriptCoreFound = false;
	for (const std::filesystem::path& candidate : scriptCoreCandidates) {
		if (pathExists(candidate)) {
			scriptCoreFound = true;
			break;
		}
	}
	if (!scriptCoreFound) {

		ok = false;
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: prerequisite missing (ScriptCore). searched={} | {} profile={}",
			ToUtf8Path(scriptCoreCandidates[0]), ToUtf8Path(scriptCoreCandidates[1]), profile);
	}

	if (!ok) {

		// --no-dependencies build は前提を作り直さない。明確な復旧手順を出す
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: cannot run the GameScripts staging build because prerequisites are missing. "
			"Rebuild Sandbox (or the Editor) for profile '{}' so NEM.ScriptCore / NEM.ScriptCodeGen are produced. "
			"The staging build uses --no-dependencies and will not generate prerequisites at runtime. "
			"ScriptCore is also already loaded by the Editor and must not be overwritten while running.", profile);
	}
	return ok;
}

void Engine::ManagedScriptBuildService::OnBuildFinished() {

	const auto buildEnd = std::chrono::steady_clock::now();
	diagnostics_.buildMs = DurationMs(buildStartTime_, buildEnd);
	diagnostics_.buildExitCode = process_.ExitCode();

	if (diagnostics_.buildExitCode != 0) {

		// build 失敗：現在の正常DLLは unload せず維持する
		// compiler error の全文は gameLogic.log にある。engine.log には原因追跡に足る要約を残す。
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: build failed. exitCode={} buildId={} (keeping the currently loaded assembly).",
			diagnostics_.buildExitCode, diagnostics_.buildId);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  cwd={}", ToUtf8Path(lastBuildWorkingDir_));
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  cmd={}", lastBuildCommandUtf8_);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  staging={}", ToUtf8Path(currentStagingDir_));
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  first error: {}", firstErrorLine_.empty() ? "(none captured)" : firstErrorLine_);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  last error: {}", lastErrorLine_.empty() ? "(none captured)" : lastErrorLine_);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  full build output is in gameLogic.log.");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// staging artifact 検証
	diagnostics_.artifactValid = ValidateArtifacts(currentStagingDir_);
	if (!diagnostics_.artifactValid) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: staged artifact validation failed (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	SetState(State::BuildSucceeded);

	// Script Manifest を staging へ生成する。対象DLLは一時 collectible ALC で読み取るだけで、
	// 現在ロード中の DLL には一切触れない。生成と検証（GUID形式・重複）に失敗したら load しない。
	const auto manifestStart = std::chrono::steady_clock::now();
	const std::filesystem::path stagedDll = currentStagingDir_ / kAssemblyFileName;
	const std::filesystem::path stagedManifest = currentStagingDir_ / kManifestFileName;
	const ManagedStatus manifestStatus = runtime_->GenerateScriptManifest(stagedDll, stagedManifest);
	diagnostics_.manifestMs = DurationMs(manifestStart, std::chrono::steady_clock::now());
	diagnostics_.manifestValid = (manifestStatus == ManagedStatus::Ok);
	if (!diagnostics_.manifestValid) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: script manifest generation/validation failed (status={}). "
			"keeping the currently loaded assembly.", static_cast<int32_t>(manifestStatus));
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// shadow copy を作成し、staging からコピーする
	const auto stagingStart = std::chrono::steady_clock::now();
	SetState(State::Staging);
	diagnostics_.reloadId = ++reloadCounter_;
	currentShadowDir_ = ShadowRoot() / std::to_wstring(diagnostics_.reloadId);
	std::error_code dirError{};
	std::filesystem::create_directories(currentShadowDir_, dirError);

	// dll/pdb/deps/runtimeconfig と一緒に manifest も shadow へコピーされる
	if (!CopyArtifacts(currentStagingDir_, currentShadowDir_) || !ValidateArtifacts(currentShadowDir_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: failed to create shadow copy (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// load 前に、shadow に manifest が確実に存在することを確認する（揃っていなければ load しない）
	std::error_code shadowManifestExists{};
	if (!std::filesystem::exists(currentShadowDir_ / kManifestFileName, shadowManifestExists) || shadowManifestExists) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: script manifest is missing in the shadow copy (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	diagnostics_.shadowCopyMs = DurationMs(stagingStart, std::chrono::steady_clock::now());

	// 実際の unload/load は次の Tick（main thread・Edit確認後）で行う
	SetState(State::ReloadPending);
}

void Engine::ManagedScriptBuildService::ApplyReload() {

	SetState(State::Reloading);

	const auto loadStart = std::chrono::steady_clock::now();
	const std::filesystem::path shadowDll = currentShadowDir_ / kAssemblyFileName;

	// 現在の assembly を unload し、shadow copy から collectible ALC へ load する
	const bool loaded = runtime_->LoadGameAssemblyFromPath(shadowDll);
	diagnostics_.loadMs = DurationMs(loadStart, std::chrono::steady_clock::now());
	diagnostics_.scriptTypeCount = runtime_->ManagedScriptTypeCount();

	if (loaded) {

		SetState(State::ReloadSucceeded);
		// type refresh まで成功したので last-known-good を更新する
		UpdateLastKnownGood(currentShadowDir_);
		PruneDirectories(StagingRoot());
		PruneDirectories(ShadowRoot());

		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptBuildService: reload succeeded. buildId={} reloadId={} changed={} "
			"buildMs={:.1f} shadowMs={:.1f} loadMs={:.1f} types={} fallback={}",
			diagnostics_.buildId, diagnostics_.reloadId, diagnostics_.changedSourceCount,
			diagnostics_.buildMs, diagnostics_.shadowCopyMs, diagnostics_.loadMs,
			diagnostics_.scriptTypeCount, diagnostics_.fallbackUsed);

		FinishCycle(true);
		return;
	}

	// reload 失敗：last-known-good から復旧を試みる
	SetState(State::ReloadFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: reload failed. buildId={} reloadId={}. attempting fallback to last-known-good.",
		diagnostics_.buildId, diagnostics_.reloadId);
	ApplyFallback();
}

void Engine::ManagedScriptBuildService::ApplyFallback() {

	SetState(State::FallbackLoading);
	diagnostics_.fallbackUsed = true;

	const std::filesystem::path lastKnownGoodDll = LastKnownGoodDirectory() / kAssemblyFileName;
	std::error_code existsError{};
	if (std::filesystem::exists(lastKnownGoodDll, existsError) && !existsError &&
		runtime_->LoadGameAssemblyFromPath(lastKnownGoodDll)) {

		SetState(State::FallbackSucceeded);
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: recovered using last-known-good assembly. path={}",
			ToUtf8Path(lastKnownGoodDll));
		// Edit は復旧したが、最新ビルドの reload には失敗しているため Play 成功とはしない
		FinishCycle(false);
		return;
	}

	SetState(State::FallbackFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: fallback to last-known-good also failed. managed scripts are unavailable until fixed.");
	FinishCycle(false);
}

void Engine::ManagedScriptBuildService::FinishCycle(bool succeeded) {

	// Play 用ビルドの結果を確定する（この cycle が forPlay の場合のみ）
	if (currentForPlay_) {
		playBuildResult_ = succeeded ? PlayBuildResult::Succeeded : PlayBuildResult::Failed;
	}
	currentForPlay_ = false;
	SetState(State::Idle);
}

bool Engine::ManagedScriptBuildService::ValidateArtifacts(const std::filesystem::path& directory) const {

	// required: GameScripts.dll
	std::error_code existsError{};
	const std::filesystem::path dll = directory / kAssemblyFileName;
	if (!std::filesystem::exists(dll, existsError) || existsError) {
		return false;
	}

	// optional: pdb / deps.json / runtimeconfig.json（欠落は warning に留める）
	const std::filesystem::path optional[] = {
		directory / L"GameScripts.pdb",
		directory / L"GameScripts.deps.json",
		directory / L"GameScripts.runtimeconfig.json",
	};
	for (const std::filesystem::path& artifact : optional) {
		std::error_code optionalError{};
		if (!std::filesystem::exists(artifact, optionalError) || optionalError) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: optional artifact is missing. path={}", ToUtf8Path(artifact));
		}
	}
	return true;
}

bool Engine::ManagedScriptBuildService::CopyArtifacts(const std::filesystem::path& from, const std::filesystem::path& to) const {

	// build 出力ディレクトリの内容を丸ごと shadow へコピーする（dll/pdb/deps/runtimeconfig/依存DLLを含む）
	std::error_code copyError{};
	std::filesystem::create_directories(to, copyError);
	std::filesystem::copy(from, to,
		std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive,
		copyError);
	if (copyError) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: artifact copy failed. from={} to={} error={}",
			ToUtf8Path(from), ToUtf8Path(to), copyError.message());
		return false;
	}
	return true;
}

void Engine::ManagedScriptBuildService::UpdateLastKnownGood(const std::filesystem::path& shadowDirectory) {

	const std::filesystem::path lkg = LastKnownGoodDirectory();

	// 旧 LKG を消してから最新の正常 shadow をコピーする。失敗は warning に留め reload は失敗させない
	std::error_code removeError{};
	std::filesystem::remove_all(lkg, removeError);
	if (!CopyArtifacts(shadowDirectory, lkg)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: failed to update last-known-good (reload itself succeeded).");
	}
}

void Engine::ManagedScriptBuildService::SeedLastKnownGood() {

	if (!runtime_) {
		return;
	}
	const std::filesystem::path active = runtime_->ActiveAssemblyPath();
	if (active.empty()) {
		return;
	}
	std::error_code existsError{};
	if (!std::filesystem::exists(active, existsError) || existsError) {
		return;
	}
	const std::filesystem::path lkg = LastKnownGoodDirectory();
	// 既に LKG があるなら seed しない（過去の正常ビルドを優先）
	std::error_code lkgExists{};
	if (std::filesystem::exists(lkg / kAssemblyFileName, lkgExists) && !lkgExists) {
		return;
	}
	CopyArtifacts(active.parent_path(), lkg);
}

std::filesystem::path Engine::ManagedScriptBuildService::ManagedRoot() const {

	// <GameRoot>/Managed を csproj パスから導出する（.../Scripts/GameScripts.csproj → GameRoot/Managed）
	const std::filesystem::path projectPath = runtime_ ? runtime_->GameScriptProjectPath() : std::filesystem::path{};
	if (projectPath.empty()) {
		return {};
	}
	return projectPath.parent_path().parent_path() / "Managed";
}

std::filesystem::path Engine::ManagedScriptBuildService::StagingRoot() const {
	return ManagedRoot() / "Staging" / BuildProfile();
}

std::filesystem::path Engine::ManagedScriptBuildService::ShadowRoot() const {
	return ManagedRoot() / "Shadow" / BuildProfile();
}

std::filesystem::path Engine::ManagedScriptBuildService::LastKnownGoodDirectory() const {
	return ManagedRoot() / "LastKnownGood" / BuildProfile();
}

void Engine::ManagedScriptBuildService::PruneDirectories(const std::filesystem::path& parent) const {

	std::error_code existsError{};
	if (!std::filesystem::exists(parent, existsError) || existsError) {
		return;
	}

	// 数値ID名のサブディレクトリを集めて新しい順に保持数だけ残す
	std::vector<std::filesystem::path> directories;
	std::error_code iterateError{};
	for (const auto& entry : std::filesystem::directory_iterator(parent, iterateError)) {
		if (entry.is_directory()) {
			directories.emplace_back(entry.path());
		}
	}
	if (static_cast<int32_t>(directories.size()) <= maxRetainedDirectories_) {
		return;
	}

	// ディレクトリ名は単調増加IDなので名前長→辞書順で安定に並ぶよう数値比較する
	std::sort(directories.begin(), directories.end(),
		[](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
			const std::wstring a = lhs.filename().wstring();
			const std::wstring b = rhs.filename().wstring();
			if (a.size() != b.size()) {
				return a.size() < b.size();
			}
			return a < b;
		});

	const size_t removeCount = directories.size() - static_cast<size_t>(maxRetainedDirectories_);
	for (size_t i = 0; i < removeCount; ++i) {

		std::error_code removeError{};
		std::filesystem::remove_all(directories[i], removeError);
		if (removeError) {
			// 掃除失敗は warning のみ。reload 本体は失敗させない
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: failed to prune old directory. path={}", ToUtf8Path(directories[i]));
		}
	}
}

void Engine::ManagedScriptBuildService::SetState(State next) {

	if (state_ == next) {
		return;
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: state {} -> {}", StateName(state_), StateName(next));
	state_ = next;
}
