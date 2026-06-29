#include "ManagedScriptBuildService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// windowsの_wcsicmp等
#include <cwctype>
// c++
#include <algorithm>
#include <cctype>
#include <ctime>
#include <vector>

namespace {

	// 現在時刻をローカル時刻のUTF-8文字列で返すスナップショット表示用
	std::string NowTimeStringUtf8() {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char buffer[32]{};
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
		return std::string(buffer);
	}

	// ビルド出力1行のseverityをコンソール色分け用のログレベルへ変換する、警告はwarnでエラーはerr
	spdlog::level::level_enum DiagnosticLogLevel(const std::string& line) {

		if (const auto diagnostic = Engine::ManagedBuildDiagnosticStore::ParseLine(line)) {

			switch (diagnostic->severity) {
			case Engine::DiagnosticSeverity::Error:   return spdlog::level::err;
			case Engine::DiagnosticSeverity::Warning: return spdlog::level::warn;
			default:                                  return spdlog::level::info;
			}
		}
		return spdlog::level::info;
	}

	// GameScriptsのアセンブリ/付随ファイル名
	constexpr const wchar_t* kAssemblyFileName = L"GameScripts.dll";
	// マニフェストは安定スクリプト型GUIDの一覧でロード前に検証する成果物
	constexpr const wchar_t* kManifestFileName = L"GameScripts.scriptmanifest.json";

	std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
	}

	std::wstring Widen(const std::string& text) {
		return Engine::Algorithm::ConvertString(text);
	}

	// 現在のビルド構成名でDebugやDevelopやRelease
	std::string BuildProfile() {
		return _PROFILE;
	}

	// 監視から外すディレクトリ名で大小無視、生成物とVCSとリロード作業領域を含む
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

	// 生成された.csかどうかで*.g.csや*.generated.csを対象にする
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

	// 状態名でログ用
	const char* StateName(Engine::ManagedScriptBuildService::State state) {
		using State = Engine::ManagedScriptBuildService::State;
		switch (state) {
		case State::Idle: return "Idle";
		case State::Debouncing: return "Debouncing";
		case State::MetadataSyncing: return "MetadataSyncing";
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

	// 出力行がコンパイラやMSBuildのエラーを含むか大小無視で判定する
	bool ContainsErrorToken(const std::string& line) {

		std::string lower;
		lower.reserve(line.size());
		for (char c : line) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		// 集計行は実エラー本文ではないので除外し実際のエラー行だけを対象にする
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

	// 起動時にソースの基準を取得する、初回は変更扱いにしない
	PollSourceChanges();
	dirty_ = false;

	// ただしロード済みアセンブリよりソースが新しければEdit中に再ビルドさせる
	// エディタ起動前に編集した.csを、Playを押さずにインスペクターへ反映するため
	if (IsSourceNewerThanLoadedAssembly()) {

		dirty_ = true;
		// すぐにビルドへ進ませるためデバウンス済み扱いにする
		lastChangeTime_ = std::chrono::steady_clock::now() - debounce_;
	}

	// 現在ロード中の正常DLLを最後の正常版として確保する、初回リロード失敗時の復旧用
	SeedLastKnownGood();
}

void Engine::ManagedScriptBuildService::Shutdown() {

	process_.Terminate();
	watcher_.Stop();
	watchedRoot_.clear();
	state_ = State::Idle;
	runtime_ = nullptr;
}

void Engine::ManagedScriptBuildService::Tick(bool playing) {

	if (!runtime_ || !runtime_->IsInitialized()) {
		return;
	}

	// 変更検知でPlay中もdirtyは記録するがリロードはしない
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

	// ビルド対象が無ければ即成功でスクリプト無しでもPlay可能
	if (!runtime_ || runtime_->GameScriptProjectPath().empty()) {
		playBuildResult_ = PlayBuildResult::Succeeded;
	}
}

void Engine::ManagedScriptBuildService::PollSourceChanges() {

	const auto now = std::chrono::steady_clock::now();

	const std::filesystem::path projectPath = runtime_->GameScriptProjectPath();
	if (projectPath.empty()) {
		sourceSnapshot_.clear();
		hasSnapshot_ = false;
		watcher_.Stop();
		watchedRoot_.clear();
		return;
	}

	// Scriptsルートと兄弟のGameAssetsを含む共通の親を監視する
	const std::filesystem::path watchRoot = projectPath.parent_path().parent_path();
	if (watchRoot != watchedRoot_) {

		watcher_.Start(watchRoot);
		watchedRoot_ = watchRoot;
		// 張り直し直後は確実に一度走査するため安全走査の期限をリセットする
		nextScanTime_ = std::chrono::steady_clock::time_point{};
	}

	// 変更を検知したか取りこぼし対策の安全走査期限が来た時だけ実走査する
	const bool changedByWatcher = watcher_.ConsumeChanged();
	if (!changedByWatcher && now < nextScanTime_) {
		return;
	}
	nextScanTime_ = now + scanInterval_;

	// csproj単体+ Scriptsルート+ GameAssetsルートを集約する
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
		// アクセス権エラーで監視全体を止めないようerror_code版で走査する
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

	// 変更判定でサイズと更新時刻と追加削除を見る
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

bool Engine::ManagedScriptBuildService::IsSourceNewerThanLoadedAssembly() const {

	if (!runtime_) {
		return false;
	}

	// 起動時に実際にロードされるアセンブリ自体の時刻を基準にする
	// Edit中ビルドはステージングへ出るためロード元のcanonicalは更新されない、ここはロード対象そのものを見る
	const std::filesystem::path assemblyPath = runtime_->ActiveAssemblyPath();
	std::error_code existsError{};
	if (assemblyPath.empty() || !std::filesystem::exists(assemblyPath, existsError) || existsError) {
		// ロード済みアセンブリが無ければ新旧を判断できないのでPlay側のビルドに任せる
		return false;
	}
	std::error_code timeError{};
	const auto assemblyTime = std::filesystem::last_write_time(assemblyPath, timeError);
	if (timeError) {
		return false;
	}

	// 監視中ソースのどれかがロード対象より新しければ起動前に編集されたとみなす
	for (const auto& [path, stamp] : sourceSnapshot_) {
		if (stamp.time > assemblyTime) {
			return true;
		}
	}
	return false;
}

void Engine::ManagedScriptBuildService::AdvanceState(bool playing) {

	switch (state_) {
	case State::Idle:
	{
		// Play用の強制ビルドを最優先で開始する
		if (!playing && playBuildRequested_ && playBuildResult_ == PlayBuildResult::Pending) {
			StartBuild(true);
			return;
		}
		// 通常のEdit変更はデバウンスへ
		if (!playing && dirty_) {
			SetState(State::Debouncing);
		}
		return;
	}
	case State::Debouncing:
	{
		// Playへ入ったら一旦保留し、変更はdirtyのままでStop後に再開する
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
	case State::MetadataSyncing:
	{
		const bool finished = process_.Poll([this](const std::string& line) {
			// 同期ツールの出力つまり採番やリネームや曖昧診断をエディタコンソールへ転送する、警告/エラーは色分けされる
			Logger::Output(LogType::GameLogic, DiagnosticLogLevel(line), "[ScriptMetaSync] {}", line);
			// 取り込み点で構造化診断ストアへ入れコンソール文字列は再解析しない
			ManagedBuildDiagnosticStore::GetInstance().Ingest(diagnostics_.buildID, diagnostics_.reloadID,
				ManagedBuildProcessKind::MetadataSync, line);
			if (ContainsErrorToken(line)) {
				if (firstErrorLine_.empty()) {
					firstErrorLine_ = line;
				}
				lastErrorLine_ = line;
			}
			});
		if (finished) {

			const int32_t exitCode = process_.ExitCode();
			if (exitCode == 0) {
				// 採番成功でステージングビルドへ
				StartGameScriptsBuild();
			} else {
				// exit 2は手動解決が必要な曖昧リネームでexit 1は失敗、いずれも現行DLLを維持して保留する
				Logger::Output(LogType::Engine, spdlog::level::err,
					"ManagedScriptBuildService: script metadata sync failed/held. exitCode={} "
					"(keeping the currently loaded assembly). first='{}' last='{}'. See gameLogic.log.",
					exitCode, firstErrorLine_.empty() ? "(none)" : firstErrorLine_,
					lastErrorLine_.empty() ? "(none)" : lastErrorLine_);
				if (currentForPlay_) {
					playBuildResult_ = PlayBuildResult::Failed;
				}
				SetState(State::BuildFailed);
				FinishCycle(false);
			}
		}
		return;
	}
	case State::Building:
	{
		const bool finished = process_.Poll([this](const std::string& line) {
			// ビルド出力をエディタコンソールつまりGameLogicログへ逐次転送する、警告は黄エラーは赤で色分けされる
			Logger::Output(LogType::GameLogic, DiagnosticLogLevel(line), "[GameScripts build] {}", line);
			// 取り込み点で構造化診断ストアへ入れMSBuildやCSCのエラーと警告を解析する
			ManagedBuildDiagnosticStore::GetInstance().Ingest(diagnostics_.buildID, diagnostics_.reloadID,
				ManagedBuildProcessKind::Build, line);
			// engine.log要約用にエラー行の最初と最後を保持する、全文はgameLogic.log側
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
		// リロードの適用はメインスレッドかつEditモードでのみ行う
		if (playing) {
			return;
		}
		ApplyReload();
		return;
	}
	default:
		// 他の状態はOnBuildFinished/ApplyReload/ApplyFallback内で同期的に遷移済み
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

	// --no-dependenciesビルドのため前提成果物の有無を先に確認する、ScriptCoreはエディタがロード済みで作り直さない
	if (!VerifyBuildPrerequisites()) {

		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		dirty_ = false;
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	// 変更は消費する、ビルド中の追加変更はPollSourceChangesが再びdirtyへ戻す
	dirty_ = false;
	currentForPlay_ = forPlay;
	buildStartTime_ = std::chrono::steady_clock::now();
	diagnostics_ = ReloadDiagnostics{};
	diagnostics_.buildID = ++buildCounter_;
	// 新しいビルドサイクルの開始を診断ストアへ通知し、古いビルドの履歴を上限で間引く
	ManagedBuildDiagnosticStore::GetInstance().BeginBuild(diagnostics_.buildID);
	firstErrorLine_.clear();
	lastErrorLine_.clear();

	// ステージングディレクトリ作成の失敗は無視せず、絶対パスとエラーを出して中断する
	std::error_code dirError{};
	currentStagingDir_ = StagingRoot() / std::to_wstring(diagnostics_.buildID);
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

	// ステージング出力へdotnet buildし実行中DLLは触らない、NEMScriptStagingOutputでこのプロジェクトだけステージングへ向ける
	pendingBuildCommand_ =
		L"dotnet build \"" + projectPath.wstring() + L"\" -c " + Widen(BuildProfile()) +
		L" --nologo --no-dependencies -p:DebugType=portable -p:DebugSymbols=true -p:Optimize=false" +
		L" -p:NEMScriptMetadataMode=EditorSync" +
		L" -p:NEMScriptStagingOutput=\"" + currentStagingDir_.wstring() + L"\"";
	lastBuildWorkingDir_ = projectPath.parent_path();

	// ビルド前に.cs.metaの安定ID採番と維持を非同期で実行する、ツールが無ければ飛ばして直接ビルドする
	// metadata sync toolはレイアウトで配置が異なるため候補を順に探す
	// エンジンソース構成: Project/Engine/Managed/NEM.ScriptMetaSync/bin/<profile>/net10.0/
	// prebuilt SDK構成: <GameRoot>/External/NEMEngine/Managed/Tools/
	const std::filesystem::path projectRoot = projectPath.parent_path().parent_path().parent_path();
	const std::filesystem::path syncToolCandidates[] = {
		projectRoot / "Engine" / "Managed" / "NEM.ScriptMetaSync" / "bin" / BuildProfile() / "net10.0" / "NEM.ScriptMetaSync.dll",
		projectRoot.parent_path() / "External" / "NEMEngine" / "Managed" / "Tools" / "NEM.ScriptMetaSync.dll",
	};
	std::filesystem::path syncToolDll;
	for (const std::filesystem::path& candidate : syncToolCandidates) {

		std::error_code candidateExists{};
		if (std::filesystem::exists(candidate, candidateExists) && !candidateExists) {
			syncToolDll = candidate;
			break;
		}
	}
	const std::filesystem::path scriptsRoot = projectPath.parent_path().parent_path() / "GameAssets";

	if (syncToolDll.empty()) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: script metadata sync tool not found ({} | {}). Skipping sync; relying on existing .cs.meta.",
			ToUtf8Path(syncToolCandidates[0]), ToUtf8Path(syncToolCandidates[1]));
		return StartGameScriptsBuild();
	}

	const std::wstring syncCommand =
		L"dotnet \"" + syncToolDll.wstring() + L"\" --root \"" + scriptsRoot.wstring() + L"\" --mode EditorSync";

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: build start. buildID={} forPlay={} staging={}",
		diagnostics_.buildID, forPlay, ToUtf8Path(currentStagingDir_));
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: metadata sync. cmd={}", Algorithm::ConvertString(syncCommand));

	if (!process_.Start(syncCommand, lastBuildWorkingDir_)) {

		// 同期を起動できないときは既存.cs.metaを前提にそのままビルドへ進む
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: failed to start metadata sync process. Proceeding to build with existing .cs.meta.");
		return StartGameScriptsBuild();
	}

	SetState(State::MetadataSyncing);
	return true;
}

bool Engine::ManagedScriptBuildService::StartGameScriptsBuild() {

	lastBuildCommandUtf8_ = Algorithm::ConvertString(pendingBuildCommand_);

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: build command. cwd={} cmd={}",
		ToUtf8Path(lastBuildWorkingDir_), lastBuildCommandUtf8_);

	if (!process_.Start(pendingBuildCommand_, lastBuildWorkingDir_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: failed to start dotnet build process. cmd={}", lastBuildCommandUtf8_);
		if (currentForPlay_) {
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

	// GameScripts.csprojの位置からエンジンのManagedディレクトリを導出する
	const std::filesystem::path projectRoot = projectPath.parent_path().parent_path().parent_path();
	const std::filesystem::path engineManagedDir = projectRoot / "Engine" / "Managed";

	std::error_code existsError{};
	if (!std::filesystem::exists(engineManagedDir, existsError) || existsError) {
		// 想定外のレイアウトつまりテンプレート等では誤検知を避けるため検証をスキップする
		return true;
	}

	const std::string profile = BuildProfile();

	// NEM.ScriptCodeGen.dllは--no-dependenciesでは作られないため必須
	const std::filesystem::path codeGenDll =
		engineManagedDir / "NEM.ScriptCodeGen" / "bin" / profile / "netstandard2.0" / "NEM.ScriptCodeGen.dll";

	// NEM.ScriptCore.dllは配置先が構成で異なるため候補を順に確認する
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

		// --no-dependenciesビルドは前提を作り直さないため明確な復旧手順を出す
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

		// ビルド失敗時は正常DLLを解放せず維持する、全文はgameLogic.logでengine.logには要約を残す
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: build failed. exitCode={} buildID={} (keeping the currently loaded assembly).",
			diagnostics_.buildExitCode, diagnostics_.buildID);
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

	// ステージング成果物の検証
	diagnostics_.artifactValid = ValidateArtifacts(currentStagingDir_);
	if (!diagnostics_.artifactValid) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: staged artifact validation failed (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	SetState(State::BuildSucceeded);

	// マニフェストをステージングへ生成する、対象DLLは一時ALCで読むだけで現行DLLに触れず検証失敗時はロードしない
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

	// シャドウコピーを作成しステージングからコピーする
	const auto stagingStart = std::chrono::steady_clock::now();
	SetState(State::Staging);
	diagnostics_.reloadID = ++reloadCounter_;
	currentShadowDir_ = ShadowRoot() / std::to_wstring(diagnostics_.reloadID);
	std::error_code dirError{};
	std::filesystem::create_directories(currentShadowDir_, dirError);

	// dll/pdb/deps/runtimeconfigと一緒にマニフェストもシャドウへコピーされる
	if (!CopyArtifacts(currentStagingDir_, currentShadowDir_) || !ValidateArtifacts(currentShadowDir_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: failed to create shadow copy (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// ロード前にシャドウにマニフェストが確実に存在するか確認し揃っていなければロードしない
	std::error_code shadowManifestExists{};
	if (!std::filesystem::exists(currentShadowDir_ / kManifestFileName, shadowManifestExists) || shadowManifestExists) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: script manifest is missing in the shadow copy (keeping the currently loaded assembly).");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	diagnostics_.shadowCopyMs = DurationMs(stagingStart, std::chrono::steady_clock::now());

	// 実際の解放とロードは次のTickで行う、メインスレッドかつEdit確認後
	SetState(State::ReloadPending);
}

void Engine::ManagedScriptBuildService::ApplyReload() {

	SetState(State::Reloading);

	const auto loadStart = std::chrono::steady_clock::now();
	const std::filesystem::path shadowDll = currentShadowDir_ / kAssemblyFileName;

	// 現在のアセンブリを解放しシャドウコピーから回収可能なALCへロードする
	const bool loaded = runtime_->LoadGameAssemblyFromPath(shadowDll);
	diagnostics_.loadMs = DurationMs(loadStart, std::chrono::steady_clock::now());
	diagnostics_.scriptTypeCount = runtime_->ManagedScriptTypeCount();

	// 旧アセンブリのALC解放の状態をログ解析せず取り込む、リロード経路のみでUnknownは正常扱いしない
	alcUnloadStatus_ = runtime_->GetLastAlcUnloadStatus();
	alcLeakSuspected_ = (alcUnloadStatus_ == AlcUnloadStatus::LeakSuspected);

	if (loaded) {

		SetState(State::ReloadSucceeded);
		// スナップショット用に最終成功時刻を記録する、ログ文字列ではなく構造化状態として保持する
		lastSuccessfulBuildTimeUtf8_ = NowTimeStringUtf8();
		lastFailureSummaryUtf8_.clear();
		// 型更新まで成功したので最後の正常版を更新する
		UpdateLastKnownGood(currentShadowDir_);
		PruneDirectories(StagingRoot());
		PruneDirectories(ShadowRoot());

		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptBuildService: reload succeeded. buildID={} reloadID={} changed={} "
			"buildMs={:.1f} manifestMs={:.1f} shadowMs={:.1f} loadMs={:.1f} types={} fallback={}",
			diagnostics_.buildID, diagnostics_.reloadID, diagnostics_.changedSourceCount,
			diagnostics_.buildMs, diagnostics_.manifestMs, diagnostics_.shadowCopyMs, diagnostics_.loadMs,
			diagnostics_.scriptTypeCount, diagnostics_.fallbackUsed);

		FinishCycle(true);
		return;
	}

	// リロード失敗、最後の正常版から復旧を試みる
	SetState(State::ReloadFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: reload failed. buildID={} reloadID={}. attempting fallback to last-known-good.",
		diagnostics_.buildID, diagnostics_.reloadID);
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
		// Editは復旧したが最新ビルドのリロードには失敗しているためPlay成功とはしない
		FinishCycle(false);
		return;
	}

	SetState(State::FallbackFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: fallback to last-known-good also failed. managed scripts are unavailable until fixed.");
	FinishCycle(false);
}

void Engine::ManagedScriptBuildService::FinishCycle(bool succeeded) {

	// Play用ビルドの結果を確定する、このサイクルがforPlayの場合のみ
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

	// 任意のpdbとdeps.jsonとruntimeconfig.jsonで欠落は警告に留める
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

	// ビルド出力ディレクトリの内容を丸ごとシャドウへコピーする、dll/pdb/deps/runtimeconfigと依存DLLを含む
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

	// LKG更新はtransaction化しincomingへcopy validateしてbackup退避と置換、失敗時rollbackで直前のusable LKGを失わない
	const std::filesystem::path lkg = LastKnownGoodDirectory();
	const std::filesystem::path incoming = lkg.string() + ".incoming";
	const std::filesystem::path backup = lkg.string() + ".old";
	std::error_code ec{};

	// 1.incomingを空にしてシャドウをコピー
	std::filesystem::remove_all(incoming, ec);
	if (!CopyArtifacts(shadowDirectory, incoming)) {
		std::filesystem::remove_all(incoming, ec);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: failed to copy shadow into incoming LKG (reload succeeded, previous LKG kept).");
		return;
	}

	// 2.必須の出力DLL等を検証してから置換する
	if (!ValidateArtifacts(incoming)) {
		std::filesystem::remove_all(incoming, ec);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: incoming LKG failed validation (previous LKG kept).");
		return;
	}

	// 3.既存LKGをbackupへ退避し、Windowsでdirectory置換が失敗しても元を失わないようにする
	const bool lkgExists = std::filesystem::exists(lkg, ec);
	if (lkgExists) {
		std::filesystem::remove_all(backup, ec);
		std::filesystem::rename(lkg, backup, ec);
		if (ec) {
			std::filesystem::remove_all(incoming, ec);
			lastKnownGoodUpdateFailed_ = true;
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: failed to back up existing LKG ({}). previous LKG kept.", ec.message());
			return;
		}
	}

	// 4. incomingをLKGへ移動し、失敗したらbackupからrollbackする
	std::filesystem::rename(incoming, lkg, ec);
	if (ec) {
		std::error_code rollbackEc{};
		if (lkgExists) {
			std::filesystem::rename(backup, lkg, rollbackEc);
		}
		std::filesystem::remove_all(incoming, rollbackEc);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: failed to install new LKG ({}). rolled back to previous LKG.", ec.message());
		return;
	}

	// 5.置換成功、backupのcleanup失敗はwarningに留める、LKGは既に正
	lastKnownGoodUpdateFailed_ = false;
	if (lkgExists) {
		std::filesystem::remove_all(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: failed to remove LKG backup ({}).", ec.message());
		}
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
	// 既にLKGがあるならseedしない、過去の正常ビルドを優先する
	std::error_code lkgExists{};
	if (std::filesystem::exists(lkg / kAssemblyFileName, lkgExists) && !lkgExists) {
		return;
	}
	CopyArtifacts(active.parent_path(), lkg);
}

std::filesystem::path Engine::ManagedScriptBuildService::ManagedRoot() const {

	// GameRootのManagedをcsprojパスから導出する、Scripts/GameScripts.csprojからGameRoot/Managedを得る
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
			// 掃除失敗は警告のみでリロード本体は失敗させない
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

	// 失敗系へ遷移したらスナップショット用の失敗要約を構造化状態として記録する、ログ再解析しない
	if (next == State::BuildFailed || next == State::ReloadFailed || next == State::FallbackFailed) {
		lastFailureSummaryUtf8_ = firstErrorLine_.empty()
			? std::string(StateName(next))
			: firstErrorLine_;
	}
}

Engine::ManagedScriptBuildService::Snapshot Engine::ManagedScriptBuildService::GetSnapshot() const {

	Snapshot snapshot{};
	snapshot.state = state_;
	snapshot.buildID = diagnostics_.buildID;
	snapshot.reloadID = diagnostics_.reloadID;
	snapshot.hasPendingSourceChanges = dirty_;
	snapshot.reloadDeferredByPlayMode = playDirtyNotified_;
	std::error_code ec{};
	snapshot.hasUsableLastKnownGood =
		std::filesystem::exists(LastKnownGoodDirectory() / kAssemblyFileName, ec) && !ec;
	snapshot.lastKnownGoodUpdateFailed = lastKnownGoodUpdateFailed_;
	snapshot.alcLeakSuspected = alcLeakSuspected_;
	snapshot.alcUnloadStatus = alcUnloadStatus_;
	snapshot.activeAssemblyPath = runtime_ ? ToUtf8Path(runtime_->ActiveAssemblyPath()) : std::string{};
	snapshot.lastSuccessfulBuildTime = lastSuccessfulBuildTimeUtf8_;
	snapshot.lastFailureSummary = lastFailureSummaryUtf8_;
	return snapshot;
}

void Engine::ManagedScriptBuildService::RequestRebuild() {

	// 次の安全地点でビルドやリロードを開始させる、状態機械は触らずdirtyを立てるだけでPlay中は保留される
	dirty_ = true;
	lastChangeTime_ = std::chrono::steady_clock::now() - debounce_;
}

void Engine::ManagedScriptBuildService::RequestRetry() {

	RequestRebuild();
}

void Engine::ManagedScriptBuildService::RequestMetadataSync() {

	// ビルドサイクルの先頭でメタデータ同期が走るため再ビルド要求と同じ経路で良い
	RequestRebuild();
}

void Engine::ManagedScriptBuildService::RequestReloadWhenSafe() {

	// 安全になった時点でリロードし、Play中は既存の保留規則つまりStop後反映に従う
	RequestRebuild();
}
