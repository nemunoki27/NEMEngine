#include "TimelineEditorIntegrationTests.h"
#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Commands/Core/EditorSelectionSnapshot.h>
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>
#include <Engine/Editor/Tools/Builtin/Timeline/TimelineEditorSession.h>
#include <Engine/Editor/Animation/AnimationClipDocument.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	// ツールの選択をシーンの寿命や配列位置から切り離す
	void CheckInspectionSelection() {

		EditorInspectionTarget target{"engine.timeline", AssetID::New(), Engine::UUID::New(), Engine::UUID::New()};
		Require(nlohmann::json(target).get<EditorInspectionTarget>() == target, "ツールの選択を往復保存する");
		const auto invalid = nlohmann::json{{"providerID", 12}, {"asset", nullptr}, {"primary", true}};
		Require(!invalid.get<EditorInspectionTarget>().IsSet(), "不正なレイアウト値を選択として採用しない");
		ECSWorld world;
		const Entity entity = world.CreateEntity();
		EditorState state;
		state.SelectEntity(entity);
		state.SelectInspectionTarget(target);
		state.ValidateSelection(&world);
		Require(state.selectionKind == EditorSelectionKind::Tool && state.selectedEntity == entity,
			"ツールInspectorを表示してもシーンの選択対象を失わない");
		state.SelectAsset(AssetID::New());
		Require(state.selectionKind == EditorSelectionKind::Asset, "Projectの選択で通常のInspectorへ戻る");
	}

	// 再生中の操作と未保存の公開をファイル保存から分ける
	void CheckTransportAndDocuments() {

		NEMTests::TestDirectory directory("TimelineEditorIntegration", RuntimePaths::GetGameAssetsRoot());
		AssetDatabase database;
		database.Init();
		std::string error;
		const AssetID id = ProjectAssetFileUtility::CreateRegisteredAsset(database,
			RuntimePaths::ToAssetPath(directory.GetPath()), ProjectAssetFileKind::Timeline, "Attack", error);
		Require(static_cast<bool>(id), "検証するTimelineを作成する");
		ECSWorld world;
		const Entity owner = world.CreateEntity();
		world.AddComponent<TransformComponent>(owner);
		world.AddComponent<TimelinePlayerComponent>(owner);
		auto& scene = world.AddComponent<SceneObjectComponent>(owner);
		scene.sourceAsset = AssetID::New();
		scene.localFileID = Engine::UUID::New();
		scene.sceneInstanceID = Engine::UUID::New();
		auto* player = TryGetTimelinePlayer(world, owner);
		player->settings.timeline = id;
		EditorContext editor;
		editor.activeWorld = &world;
		editor.assetDatabase = &database;
		EditorState state;
		EditorPanelContext panel;
		panel.editorContext = &editor;
		panel.editorState = &state;
		auto context = MakeEditorToolContext(panel);
		TimelineEditorSession session;
		Require(session.Load(context, id), "作成したTimelineを編集元へ読み込む");
		session.SetOwner(context, world.GetUUID(owner));
		const auto diskRevision = database.GetContentRevision(id);
		session.GetAsset().name = "未保存の攻撃";
		session.MarkChanged();
		session.CommitHistory();
		session.PublishDraft(context);
		Require(session.IsDirty() && database.GetContentRevision(id) == diskRevision,
			"未保存の公開でファイルの変更通知を発生させない");
		session.RestoreHistory(false);
		Require(!session.IsDirty(), "Timelineの編集を保存時の内容へ戻す");
		session.RestoreHistory(true);
		Require(session.GetAsset().name == "未保存の攻撃", "Timelineの編集をやり直す");
		const AssetID sourceID = ProjectAssetFileUtility::CreateRegisteredAsset(database,
			RuntimePaths::ToAssetPath(directory.GetPath()), ProjectAssetFileKind::AnimationClip, "Source", error);
		const auto source = state.animationClipDocuments->Acquire(database, sourceID, error);
		Require(static_cast<bool>(source), "同じ履歴へ素材の編集を接続する");
		source->GetClip().name = "記録した色";
		source->MarkDirty();
		source->CommitHistory();
		session.GetAsset().name = "配置調整";
		session.MarkChanged();
		session.CommitHistory();
		session.RestoreHistory(false);
		Require(session.GetAsset().name == "未保存の攻撃" && source->GetClip().name == "記録した色",
			"素材より後のTimeline編集を先に取り消す");
		session.RestoreHistory(false);
		Require(source->GetClip().name == "Source", "次に素材の編集を取り消す");
		session.RestoreHistory(true);
		Require(source->GetClip().name == "記録した色", "編集順に素材をやり直す");
		session.RestoreHistory(true);
		Require(session.GetAsset().name == "配置調整", "編集順にTimelineをやり直す");
		session.GetAsset().durationMode = TimelineDurationMode::FixedLength;
		session.GetAsset().duration = 2.0;
		session.MarkChanged();
		editor.isPlaying = true;
		context = MakeEditorToolContext(panel);
		player->runtime.playing = true;
		session.TogglePlayback(context);
		Require(player->runtime.request == 2, "Play中の一時停止を実Playerへ送る");
		player->runtime.paused = true;
		session.TogglePlayback(context);
		Require(player->runtime.request == 3, "Play中の再開を実Playerへ送る");
		session.Seek(context, -5.0);
		Require(player->runtime.request == 6 && player->runtime.seekTime == 0.0, "Seekを有効範囲へ制限する");
		player->runtime.time = 0.25;
		session.SyncPlayback(context);
		Require(session.time == 0.0 && !session.playing,
			"未評価のSeekを前フレームの再生位置で上書きしない");
		session.Seek(context, 0.117);
		session.Seek(context, 0.123);
		session.SyncPlayback(context);
		Require(session.time == 0.123 && player->runtime.seekTime == 0.123 && player->runtime.paused,
			"フレーム未満の最新Seekだけを実Playerへ渡して一時停止する");
		player->runtime.request = 0;
		session.SyncPlayback(context);
		Require(session.time == 0.25 && !session.playing, "一時停止中の実再生位置を表示する");
		session.Seek(context, 0.7);
		session.TogglePlayback(context);
		session.SyncPlayback(context);
		Require(player->runtime.request == 6 && session.time == 0.7 && session.playing,
			"Seek直後の再生で指定時刻を上書きしない");
		player->runtime.time = player->runtime.seekTime;
		player->runtime.request = 0;
		session.SyncPlayback(context);
		session.SyncPlayback(context);
		Require(player->runtime.request == 3 && session.time == 0.7 && session.playing,
			"Seekを評価した後に再生を再開する");
		session.StopPlayback(context);
		Require(player->runtime.request == 4, "Play中の停止を実Playerへ送る");
		ECSWorld playWorld(ECSWorldKind::Runtime);
		const Entity playOwner = playWorld.CreateEntity();
		playWorld.AddComponent<SceneObjectComponent>(playOwner) = scene;
		playWorld.AddComponent<TimelinePlayerComponent>(playOwner);
		auto* playPlayer = TryGetTimelinePlayer(playWorld, playOwner);
		playPlayer->settings.timeline = id;
		editor.activeWorld = &playWorld;
		context = MakeEditorToolContext(panel);
		Require(session.GetOwner(context) == playOwner, "Playの異なるWorld IDでもシーン内の再生対象を維持する");
		session.Seek(context, 0.5);
		Require(playPlayer->runtime.request == 6, "World切替後の実Playerへ再生操作を送る");
		editor.activeWorld = nullptr;
		context = MakeEditorToolContext(panel);
		session.TogglePlayback(context);
		session.StopPlayback(context);
		session.Seek(context, 0.5);
		session.Step(context, 1);
		Require(player->runtime.request == 4, "World切替中は再生操作を送らない");
	}
}

bool NEMTests::TestTimelineEditorIntegration() {

	try {
		CheckInspectionSelection();
		CheckTransportAndDocuments();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "TimelineEditorIntegration: " << error.what() << '\n';
		return false;
	}
}
