#include "TimelineManagedTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayback.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Behavior/MonoBehavior.h>
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Animation/TimelineSystem.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <cmath>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	std::string FieldID(int index) {

		return std::format("48bbef01-32c4-42c4-a1c8-{:012}", index);
	}

	// 実橋を通した結果だけを監視用Scriptから取得する
	nlohmann::json ReadProbe(MonoBehavior& probe) {

		auto state = probe.GetRuntimeSerializedState();
		if (!state.value(FieldID(13), std::string{}).empty())
			throw std::runtime_error(state.at(FieldID(13)).get<std::string>());
		return state;
	}

	TimelineClip MakeClip(double start, double duration, std::string name) {

		TimelineClip clip;
		clip.id = Engine::UUID::New();
		clip.start = start;
		clip.duration = duration;
		clip.name = std::move(name);
		return clip;
	}

	TimelineAsset MakeAsset() {

		auto asset = CreateDefaultTimelineAsset("Managed contract");
		asset.durationMode = TimelineDurationMode::FixedLength;
		asset.duration = 1.0;
		asset.bindings.push_back({Engine::UUID::New(), "Target", "Transform"});
		asset.parameters.push_back({Engine::UUID::New(), "Power", 1.0f});
		asset.parameters.push_back({Engine::UUID::New(), "Label", std::string("initial")});
		TimelineTrack custom;
		custom.id = Engine::UUID::New();
		custom.name = "Custom";
		custom.kind = TimelineTrackKind::Custom;
		custom.binding = asset.bindings.front().id;
		auto sample = MakeClip(0, 1, "Callback");
		sample.data = {{"typeName", "tests.timeline.integration"},
			{"fields", {{"amplitude", 2.0f}, {"target", {{"binding", "Target"}}}}}};
		custom.clips.push_back(sample);
		asset.tracks.push_back(custom);
		TimelineTrack events;
		events.id = Engine::UUID::New();
		events.name = "Events";
		events.kind = TimelineTrackKind::Event;
		auto marker = MakeClip(0.25, 0, "Strike");
		marker.data = {{"name", "Strike"}};
		events.clips.push_back(marker);
		asset.tracks.push_back(events);
		TimelineTrack windows;
		windows.id = Engine::UUID::New();
		windows.name = "Windows";
		windows.kind = TimelineTrackKind::Window;
		auto window = MakeClip(0.2, 0.3, "Combo");
		window.data = {{"name", "Combo"}};
		windows.clips.push_back(window);
		asset.tracks.push_back(windows);
		return asset;
	}

	struct ManagedTimelineCase {

		std::filesystem::path root;
		AssetDatabase& database;
		ECSWorld& world;
		Entity owner;
		MonoBehavior& probe;
		BehaviorSystem& behaviors;
		TimelineSystem& timelines;
		SystemContext& context;

		AssetID Save(std::string_view name, const TimelineAsset& asset) {

			const auto path = root / (std::string(name) + ".timeline.json");
			Require(SaveTimelineAsset(path, asset) && database.RebuildMeta({root}), "追加のTimelineを登録できる");
			const auto* meta = database.FindByPath(RuntimePaths::ToAssetPath(path));
			Require(meta != nullptr, "追加のTimelineが索引に含まれる");
			return meta->guid;
		}

		void Start(AssetID asset) {

			auto* player = TryGetTimelinePlayer(world, owner);
			ReleaseTimelinePlayer(world, owner, *player, context);
			player->settings.timeline = asset;
			probe.SetRuntimeSerializedField(world, FieldID(1), 1);
			behaviors.Update(world, context);
			Tick(0);
			behaviors.Update(world, context);
			ReadProbe(probe);
		}

		void Tick(float delta) {

			context.fixedDeltaTime = delta;
			timelines.FixedUpdate(world, context);
			const auto* player = TryGetTimelinePlayer(world, owner);
			Require(player && player->runtime.diagnostic.empty(), player ? player->runtime.diagnostic.c_str() : "Playerが失効しました");
		}

		nlohmann::json Consume() {

			behaviors.Update(world, context);
			return ReadProbe(probe);
		}
	};

	TimelineAsset EmptyAsset() {

		auto asset = MakeAsset();
		asset.tracks.clear();
		return asset;
	}

	TimelineTrack EventTrack(std::initializer_list<double> times) {

		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.kind = TimelineTrackKind::Event;
		track.name = "Signals";
		for (double time : times) {
			auto clip = MakeClip(time, 0, "Strike");
			clip.data = {{"name", "Strike"}};
			track.clips.push_back(clip);
		}
		return track;
	}

	TimelineAsset ParentAsset(AssetID child, double duration, TimelineClip clip) {

		auto asset = EmptyAsset();
		asset.duration = duration;
		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.kind = TimelineTrackKind::SubTimeline;
		track.name = "Child";
		track.binding = asset.bindings.front().id;
		clip.data["timeline"] = ToAssetReferenceJson(child);
		track.clips.push_back(clip);
		asset.tracks.push_back(track);
		return asset;
	}

	// 保存Fieldと参照定義の編集では独自個体の開始処理を再実行しない
	void CheckLiveCustomConfiguration(ManagedTimelineCase& fixture) {

		auto asset = MakeAsset();
		const auto id = fixture.Save("LiveCustom", asset);
		fixture.Start(id);
		fixture.Tick(0.2f);
		const auto before = fixture.Consume();
		const int entered = before.value(FieldID(7), 0), exited = before.value(FieldID(8), 0);
		auto edited = std::make_shared<TimelineAsset>(asset);
		edited->guid = id;
		edited->tracks.front().clips.front().data["fields"]["amplitude"] = 5.0f;
		edited->bindings.push_back({Engine::UUID::New(), "AdditionalTarget", "Transform"});
		edited->parameters.push_back({Engine::UUID::New(), "AdditionalValue", 1.0f});
		Require(PublishTimelineAssetOverride(fixture.world, id, edited), "保存前の独自設定を公開する");
		fixture.Tick(0);
		const auto after = fixture.Consume();
		Require(after.value(FieldID(7), 0) == entered && after.value(FieldID(8), 0) == exited,
			"設定の編集でOnEnterとOnExitを再実行しない");
		Require(std::abs(fixture.world.GetComponent<TransformComponent>(fixture.owner).localPos.x - 3.0f) < 1e-4f,
			"同じ独自個体へ編集した保存Fieldを即反映する");
		Require(std::abs(after.value(FieldID(2), -1.0f) - 0.2f) < 1e-5f,
			"参照定義を増やしても再生時刻を戻さない");
		ClearTimelineAssetOverride(fixture.world, id);
		fixture.Tick(0);
		Require(fixture.Consume().value(FieldID(7), 0) == entered, "編集の破棄でも開始処理を再実行しない");
	}

	// Nativeの時刻変更から管理側の同じ個体へスクラブ状態を渡す
	void CheckCustomScrubbing(ManagedTimelineCase& fixture) {

		const auto id = fixture.Save("ScrubCustom", MakeAsset());
		fixture.Start(id);
		fixture.Tick(0.2f);
		const auto before = fixture.Consume();
		const int enters = before.value(FieldID(7), 0), exits = before.value(FieldID(8), 0), events = before.value(FieldID(6), 0);
		auto* player = TryGetTimelinePlayer(fixture.world, fixture.owner);
		for (double time : {0.6, 0.1, 0.4, 0.4}) {
			player->runtime.seekTime = time;
			player->runtime.request = 6;
			fixture.Tick(0.1f);
			const auto state = fixture.Consume();
			Require(state.value(FieldID(7), 0) == enters && state.value(FieldID(8), 0) == exits && state.value(FieldID(6), 0) == events,
				"前後の時刻変更で独自個体の開始と終了と通知を繰り返さない");
			Require(state.value(FieldID(16), false) && state.value(FieldID(17), -1.0f) == 0 &&
				std::abs(state.value(FieldID(18), -1.0f) - time) < 1e-5 && player->runtime.paused,
				"スクラブを一時停止しIsScrubbingと絶対時刻と時間差ゼロをC#へ渡す");
		}
	}

	// 素材開始位置より前の通知を再生せず、親の時間へ変換する
	void CheckSubTimelinePreroll(ManagedTimelineCase& fixture) {

		auto child = EmptyAsset();
		child.tracks.push_back(EventTrack({0.1, 0.6}));
		TimelineTrack windows;
		windows.id = Engine::UUID::New();
		windows.kind = TimelineTrackKind::Window;
		windows.name = "Windows";
		auto window = MakeClip(0.55, 0.15, "Combo");
		window.data = {{"name", "Combo"}};
		windows.clips.push_back(window);
		child.tracks.push_back(windows);
		const auto childID = fixture.Save("PrerollChild", child);
		auto clip = MakeClip(0.2, 0.5, "Cut in");
		clip.clipIn = 0.4;
		clip.speed = 2;
		fixture.Start(fixture.Save("PrerollParent", ParentAsset(childID, 1, clip)));
		const auto before = ReadProbe(fixture.probe).value(FieldID(6), 0);
		fixture.Tick(0.2f);
		Require(TryGetTimelinePlayer(fixture.world, fixture.owner)->runtime.notifications.empty(), "clipIn以前の子通知を発火しない");
		fixture.Consume();
		fixture.Tick(0.1f);
		const auto& events = TryGetTimelinePlayer(fixture.world, fixture.owner)->runtime.notifications;
		Require(events.size() == 1 && std::abs(events.front().time - 0.3) < 1e-5, "子の素材時刻を親の開始位置と速度で変換する");
		const auto state = fixture.Consume();
		Require(state.value(FieldID(6), 0) == before + 1 && state.value(FieldID(5), false), "C#でプリロール後の子通知と受付区間を取得する");
	}

	// 長い更新で複数周回しても親と子の通知を時刻順に取得する
	void CheckSubTimelineLoops(ManagedTimelineCase& fixture) {

		auto child = EmptyAsset();
		child.duration = 0.4;
		child.tracks.push_back(EventTrack({0.1, 0.3}));
		const auto childID = fixture.Save("LoopChild", child);
		auto clip = MakeClip(0.2, 1.2, "Loop");
		clip.data = {{"loop", true}};
		auto parent = ParentAsset(childID, 1.5, clip);
		parent.tracks.push_back(EventTrack({0.65}));
		fixture.Start(fixture.Save("LoopParent", parent));
		const auto before = ReadProbe(fixture.probe).value(FieldID(6), 0);
		fixture.Tick(1.35f);
		const double expected[]{0.3, 0.5, 0.65, 0.7, 0.9, 1.1, 1.3};
		const auto& events = TryGetTimelinePlayer(fixture.world, fixture.owner)->runtime.notifications;
		Require(events.size() == std::size(expected), "複数周回した子通知をすべて転送する");
		for (size_t i = 0; i < events.size(); ++i)
			Require(std::abs(events[i].time - expected[i]) < 1e-5, "親と子の通知を親時刻の昇順に並べる");
		Require(fixture.Consume().value(FieldID(6), 0) == before + 7, "C#で周回した通知を一度ずつ消費する");
	}

	TimelineAsset PropertyAsset(float value, double duration) {

		auto asset = EmptyAsset();
		asset.duration = duration;
		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.kind = TimelineTrackKind::Property;
		track.name = "Position";
		track.binding = asset.bindings.front().id;
		auto clip = MakeClip(0, duration, "Position");
		clip.data = CreateDefaultTimelineClipData(track.kind);
		clip.data["from"] = clip.data["to"] = Vector3(value, 0, 0).ToJson();
		clip.data["restoreOnEnd"] = false;
		track.clips.push_back(clip);
		asset.tracks.push_back(track);
		return asset;
	}

	// 子の開始を新しいPlayerの開始順と誤認しない
	void CheckInheritedOwnership(ManagedTimelineCase& fixture) {

		const auto child = fixture.Save("OwnershipChild", PropertyAsset(100, 1));
		fixture.Start(fixture.Save("OwnershipParent", ParentAsset(child, 2, MakeClip(0.5, 1, "Delayed child"))));
		auto newer = PropertyAsset(200, 2);
		const auto newerID = fixture.Save("NewerOwner", newer);
		const auto owner = SceneAuthoring::CreateGameObject(fixture.world, "NewerPlayer");
		fixture.world.AddComponent<TimelinePlayerComponent>(owner);
		auto* player = TryGetTimelinePlayer(fixture.world, owner);
		player->settings.timeline = newerID;
		player->settings.bindings.push_back({newer.bindings.front().id,
			SceneEntityReference::FromEntity(fixture.world, fixture.owner), fixture.world.GetUUID(fixture.owner)});
		player->runtime.request = 1;
		fixture.Tick(0);
		fixture.Tick(0.75f);
		Require(std::abs(fixture.world.GetComponent<TransformComponent>(fixture.owner).localPos.x - 200) < 1e-4f,
			"古い親の子Clipが開始しても同priorityの新しいPlayerを上書きしない");
		fixture.Consume();
		player->runtime.request = 4;
		fixture.Tick(0);
		Require(std::abs(fixture.world.GetComponent<TransformComponent>(fixture.owner).localPos.x - 100) < 1e-4f,
			"新しいPlayerの停止後に古い親の制御へ戻る");
	}

	// ユーザーのGameAssetsを変更せず専用プロジェクトを索引へ接続する
	bool CheckManagedTimeline(const std::filesystem::path& root) {

		RegisterBuiltinAnimationProperties();
		std::filesystem::create_directories(root / "GameAssets");
		Require(JsonFile::Save(root / "Timeline.nemproject", {{"schemaVersion", 1},
			{"projectGuid", "19d30e3f270c479897a96f1aeb663a3a"}, {"name", "Timeline"},
			{"sceneStorage", "Monolithic"}, {"assetsDirectory", "GameAssets"},
			{"packagesDirectory", "Packages"}, {"projectSettingsDirectory", "ProjectSettings"}}), "専用プロジェクトを作成できる");
		const auto previous = std::filesystem::current_path();
		ScopedCleanup restore([&]() noexcept {
			std::error_code error;
			std::filesystem::current_path(previous, error);
			try { RuntimePaths::Refresh(); } catch (...) { }
		});
		const bool ownsLogger = !Logger::Get(LogType::Engine);
		const auto previousLogger = spdlog::default_logger();
		if (ownsLogger) Logger::CreateLogFiles(root / "Logs");
		ScopedCleanup closeLog([&]() noexcept {
			if (!ownsLogger) return;
			Logger::Finalize();
			spdlog::set_default_logger(previousLogger);
		});
		std::filesystem::current_path(root);
		RuntimePaths::Refresh();
		Require(RuntimePaths::GetGameRoot() == root, "--projectを付けずに専用プロジェクトで検証してください");
		const auto assetPath = root / "GameAssets/Contract.timeline.json";
		const auto childPath = root / "GameAssets/Child.timeline.json";
		auto asset = MakeAsset();
		auto child = asset;
		child.name = "Child";
		asset.tracks.resize(1);
		Require(SaveTimelineAsset(childPath, child), "検証用の子Timelineを保存できる");
		AssetDatabase database;
		Require(database.Init() && database.RebuildMeta({root / "GameAssets"}), "Timelineを索引へ登録できる");
		const auto* childMeta = database.FindByPath(RuntimePaths::ToAssetPath(childPath));
		Require(childMeta != nullptr, "子Timelineを索引へ登録できる");
		asset.tracks.front().kind = TimelineTrackKind::SubTimeline;
		asset.tracks.front().clips.front().data = {{"timeline", ToAssetReferenceJson(childMeta->guid)}};
		Require(SaveTimelineAsset(assetPath, asset) && database.RebuildMeta({root / "GameAssets"}), "検証用Timelineを保存できる");
		const auto* meta = database.FindByPath(RuntimePaths::ToAssetPath(assetPath));
		Require(meta && meta->type == AssetType::Timeline, "Timelineのアセット種別を認識する");
		auto& runtime = ManagedScriptRuntime::GetInstance();
		const auto assembly = Algorithm::GetExecutablePath().parent_path() / "ManagedFixtures/GameScripts.dll";
		Require(runtime.Init(), "Managed Runtimeを初期化できる");
		ScopedCleanup finalize([&]() noexcept { runtime.Finalize(); });
		Require(runtime.LoadGameAssemblyFromPath(assembly, false), "検証用Assemblyを読み込める");
		const auto* type = BehaviorTypeRegistry::GetInstance().FindByName("NEMTests.Scripts.TimelineAPIProbe");
		Require(type != nullptr, "監視用Scriptを登録できる");
		const auto typeID = type->scriptTypeID;
		ECSWorld world(ECSWorldKind::Runtime);
		const auto worldHandle = ManagedWorldRegistry::GetInstance().Register(world);
		ScopedCleanup unregister([&]() noexcept { ManagedWorldRegistry::GetInstance().Unregister(worldHandle); });
		const auto owner = SceneAuthoring::CreateGameObject(world, "TimelineActor");
		SceneAuthoring::CreateGameObject(world, "TimelineTarget");
		SceneAuthoring::CreateGameObject(world, "TimelineReplacement");
		world.AddComponent<TimelinePlayerComponent>(owner);
		TryGetTimelinePlayer(world, owner)->settings.timeline = meta->guid;
		SystemContext context;
		context.world = &world;
		context.assetDatabase = &database;
		context.mode = WorldMode::Play;
		context.deltaTime = context.unscaledDeltaTime = context.fixedDeltaTime = 0.25f;
		BehaviorSystem behaviors;
		TimelineSystem timelines;
		behaviors.OnWorldEnter(world, context);
		ScopedCleanup exit([&]() noexcept {
			try { timelines.OnWorldExit(world, context); } catch (...) { }
			try { behaviors.OnWorldExit(world, context); } catch (...) { }
		});
		auto* probe = BehaviorSystem::AttachScript(owner, typeID, context);
		Require(probe != nullptr, "監視用Scriptを取り付けられる");
		auto command = [&](int request) {
			probe->SetRuntimeSerializedField(world, FieldID(1), request);
			behaviors.Update(world, context);
			timelines.FixedUpdate(world, context);
			behaviors.Update(world, context);
			const auto* player = TryGetTimelinePlayer(world, owner);
			Require(player && player->runtime.diagnostic.empty(), player ? player->runtime.diagnostic.c_str() : "Playerが失効しました");
			return ReadProbe(*probe);
		};
		auto state = command(1);
		Require(std::abs(state.value(FieldID(2), -1.0f) - 0.25f) < 1e-5f && state.value(FieldID(4), false) &&
			state.value(FieldID(5), false) && state.value(FieldID(6), 0) == 1 && state.value(FieldID(7), 0) == 1,
			"C#から再生し受付区間と通知とEnterを取得できる");
		Require(std::abs(world.GetComponent<TransformComponent>(owner).localPos.x - 1.5f) < 1e-4f,
			"独自ClipからNative Transformへ書き込める");
		state = command(2);
		Require(state.value(FieldID(3), false) && std::abs(state.value(FieldID(2), -1.0f) - 0.25f) < 1e-5f,
			"一時停止で時刻を維持する");
		state = command(4);
		Require(state.value(FieldID(3), false) && state.value(FieldID(5), false) && state.value(FieldID(6), 0) == 1 &&
			std::abs(state.value(FieldID(2), -1.0f) - 0.35f) < 1e-5f, "Seekは通知を再発火せず受付状態を更新する");
		state = command(7);
		Require(state.value(FieldID(10), "") == "TimelineReplacement" && state.value(FieldID(11), "") == "TimelineReplacement" &&
			state.value(FieldID(12), "") == "updated" && std::abs(world.GetComponent<TransformComponent>(owner).localPos.x - 2.8f) < 1e-4f,
			"現在の割り当て・保存参照Field・Float・Stringを独自Clipへ同期する");
		state = command(3);
		Require(!state.value(FieldID(3), true) && !state.value(FieldID(5), true) && state.value(FieldID(6), 0) == 1 &&
			std::abs(state.value(FieldID(2), -1.0f) - 0.6f) < 1e-5f, "再開後も通知を重複せず区間を閉じる");
		state = command(5);
		Require(!state.value(FieldID(4), true) && state.value(FieldID(8), 0) >= 1 &&
			state.value(FieldID(15), -1) == static_cast<int>(TimelineExitReason::Stopped), "停止時に独自Clipを終了する");
		state = command(6);
		Require(!state.value(FieldID(4), true) && state.value(FieldID(2), -1.0f) == 0.0f, "Resetで先頭へ戻す");
		command(1);
		for (int i = 0; i < 3; ++i) { timelines.FixedUpdate(world, context); behaviors.Update(world, context); }
		state = ReadProbe(*probe);
		Require(state.value(FieldID(14), false) && !state.value(FieldID(4), true) && state.value(FieldID(6), 0) == 2 &&
			state.value(FieldID(15), -1) == static_cast<int>(TimelineExitReason::Completed), "再再生と自然終了をC#から取得できる");
		ManagedTimelineCase fixture{root / "GameAssets", database, world, owner, *probe, behaviors, timelines, context};
		CheckLiveCustomConfiguration(fixture);
		CheckCustomScrubbing(fixture);
		CheckSubTimelinePreroll(fixture);
		CheckSubTimelineLoops(fixture);
		CheckInheritedOwnership(fixture);
		return true;
	}
}

bool NEMTests::TestTimelineManagedIntegration() {

	try {
		TestDirectory directory("TimelineManaged");
		return CheckManagedTimeline(directory.GetPath());
	} catch (const std::exception& error) {
		std::cerr << "[TimelineManaged] " << error.what() << '\n';
		return false;
	}
}
