#include "TimelineEditorTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Timeline/TimelineEditOperations.h>

// c++
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	TimelineClip MakeClip(double start, double duration) {

		TimelineClip clip;
		clip.id = Engine::UUID::New();
		clip.start = start;
		clip.duration = duration;
		return clip;
	}

	TimelineAsset MakeAsset() {

		TimelineAsset asset;
		asset.duration = 20.0;
		asset.frameRate = 60.0;
		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.kind = TimelineTrackKind::SkinnedAnimation;
		asset.tracks.push_back(std::move(track));
		return asset;
	}

	void CheckMoveAndTrim() {

		auto before = MakeAsset();
		auto& track = before.tracks.front();
		track.clips = {MakeClip(2, 3), MakeClip(6, 2)};
		const TimelineClipSelection selected[]{ {track.id, track.clips[0].id}, {track.id, track.clips[1].id} };
		auto asset = before;
		Require(TimelineEditOperations::Apply(asset, before, selected, track.id, track.clips[0].id,
			TimelineClipEditKind::Move, -10, TimelineEditMode::Mix, false), "複数移動を適用できる");
		Require(asset.tracks[0].clips[0].start == 0 && asset.tracks[0].clips[1].start == 4, "先頭0で止めても相対間隔を保つ");
		TimelineEditOperations::Apply(asset, before, selected, track.id, track.clips[0].id,
			TimelineClipEditKind::Move, 0, TimelineEditMode::Mix, false);
		Require(nlohmann::json(asset) == nlohmann::json(before), "開始位置へ戻したドラッグは元の値へ戻る");
		track.clips[0].clipIn = 1;
		track.clips[0].speed = 2;
		asset = before;
		TimelineEditOperations::Apply(asset, before, {selected, 1}, track.id, track.clips[0].id,
			TimelineClipEditKind::Start, 1, TimelineEditMode::Mix, false);
		const auto& trimmed = asset.tracks[0].clips[0];
		Require(trimmed.start == 3 && trimmed.duration == 2 && trimmed.clipIn == 3, "左端トリムは素材の開始時刻を保つ");
		Require(trimmed.sourceDuration == 3, "トリム後も素材評価の長さを維持する");
		asset = before;
		TimelineEditOperations::Apply(asset, before, {selected, 1}, track.id, track.clips[0].id,
			TimelineClipEditKind::Start, -8, TimelineEditMode::Mix, false);
		Require(asset.tracks[0].clips[0].clipIn == 0 && asset.tracks[0].clips[0].start == 1.5, "素材の負の時刻まで伸ばさない");
		before.tracks[0].locked = true;
		asset = before;
		TimelineEditOperations::Apply(asset, before, selected, track.id, track.clips[0].id,
			TimelineClipEditKind::Move, 3, TimelineEditMode::Mix, false);
		Require(nlohmann::json(asset) == nlohmann::json(before), "ロックしたトラックは移動しない");
	}

	void CheckRippleAndReplace() {

		auto before = MakeAsset();
		auto& track = before.tracks.front();
		track.clips = {MakeClip(1, 2), MakeClip(4, 1), MakeClip(7, 1)};
		const TimelineClipSelection selected{track.id, track.clips[0].id};
		auto asset = before;
		TimelineEditOperations::Apply(asset, before, {&selected, 1}, track.id, selected.clip,
			TimelineClipEditKind::End, 2, TimelineEditMode::Ripple, false);
		Require(asset.tracks[0].clips[0].duration == 4 && asset.tracks[0].clips[1].start == 6 &&
			asset.tracks[0].clips[2].start == 9, "Rippleは後続の間隔を保って伸縮する");
		asset = before;
		Require(TimelineEditOperations::Insert(asset, track.id, MakeClip(4, 2), TimelineEditMode::Ripple, false),
			"貼り付けにもRippleを適用できる");
		Require(asset.tracks[0].clips[1].start == 6 && asset.tracks[0].clips[2].start == 9,
			"貼り付けた長さだけ後続を移動する");
		track.clips = {MakeClip(0, 10), MakeClip(12, 2)};
		track.clips[0].clipIn = 1;
		track.clips[0].speed = 2;
		const TimelineClipSelection replacement{track.id, track.clips[1].id};
		asset = before;
		Require(TimelineEditOperations::Apply(asset, before, {&replacement, 1}, track.id, replacement.clip,
			TimelineClipEditKind::Move, -8, TimelineEditMode::Replace, false), "Replaceで区間を置き換える");
		Require(asset.tracks[0].clips.size() == 3, "置き換え区間の左右に素材が残る");
		const auto right = std::find_if(asset.tracks[0].clips.begin(), asset.tracks[0].clips.end(),
			[](const auto& value) { return value.start == 6; });
		Require(right != asset.tracks[0].clips.end() && right->duration == 4 && right->clipIn == 13 && right->sourceDuration == 10,
			"右側の断片は素材時刻と元の素材長を維持する");
		Require(right->id != track.clips[0].id, "分割した右側は新しい識別子を持つ");
		before.tracks[0].kind = TimelineTrackKind::Prefab;
		asset = before;
		Require(!TimelineEditOperations::Apply(asset, before, {&replacement, 1}, track.id, replacement.clip,
			TimelineClipEditKind::Move, -8, TimelineEditMode::Replace, false), "生成クリップは意図せず分割しない");
		Require(nlohmann::json(asset) == nlohmann::json(before), "未対応の置き換えは元のデータを保つ");
	}

	void CheckSplitAndSnap() {

		auto asset = MakeAsset();
		auto& track = asset.tracks.front();
		track.clips = {MakeClip(1, 4), MakeClip(8, 2)};
		track.clips[0].clipIn = 0.5;
		track.clips[0].speed = 1.5;
		const Engine::UUID trackID = track.id, clipID = track.clips[0].id;
		Require(TimelineEditOperations::Split(asset, trackID, clipID, 3), "素材クリップを再生位置で分割できる");
		Require(track.clips.size() == 3 && track.clips[0].duration == 2 && track.clips[2].start == 3 &&
			track.clips[2].clipIn == 3.5 && track.clips[2].sourceDuration == 4, "分割前後で同じ素材時刻を再生する");
		Require(!TimelineEditOperations::Split(asset, trackID, clipID, 1), "空の断片ができる分割を拒否する");
		const TimelineClipSelection selection{trackID, clipID};
		Require(TimelineEditOperations::SnapTime(asset, {&selection, 1}, 7.95, 6, 0.1) == 8,
			"別クリップの端へ吸着する");
		Require(TimelineEditOperations::SnapTime(asset, {&selection, 1}, 6.04, 6, 0.1) == 6,
			"再生ヘッドへ吸着する");
		Require(TimelineEditOperations::SnapTime(asset, {&selection, 1}, 4.25, 6, 0.1) == 4.25,
			"吸着範囲外の時刻は維持する");
	}

	void CheckTrackReordering() {

		auto asset = MakeAsset();
		auto second = asset.tracks.front(); second.id = Engine::UUID::New();
		auto third = second; third.id = Engine::UUID::New();
		const auto firstID = asset.tracks.front().id;
		asset.tracks.push_back(second); asset.tracks.push_back(third);
		Require(TimelineEditOperations::MoveTrack(asset, firstID, second.id, TimelineTrackDropPosition::After),
			"上から下へ隣のトラックを移動できる");
		Require(asset.tracks[0].id == second.id && asset.tracks[1].id == firstID, "下方向はドロップ先の後ろへ入る");
		Require(TimelineEditOperations::MoveTrack(asset, firstID, second.id, TimelineTrackDropPosition::Before),
			"下から上へ隣のトラックを移動できる");
		Require(asset.tracks[0].id == firstID && asset.tracks[1].id == second.id, "上方向はドロップ先の前へ入る");
		Require(TimelineEditOperations::MoveTrack(asset, firstID, third.id, TimelineTrackDropPosition::After),
			"最後のトラックより後ろへ移動できる");
		Require(asset.tracks.back().id == firstID, "末尾のドロップで順番が戻らない");

		auto group = second; group.id = Engine::UUID::New(); group.kind = TimelineTrackKind::Group;
		auto child = third; child.id = Engine::UUID::New(); child.parent = group.id;
		asset.tracks = {group, second, child, third};
		Require(TimelineEditOperations::MoveTrack(asset, child.id, second.id, TimelineTrackDropPosition::After),
			"子の格納位置に関係なく指定した兄弟の後ろへ移動できる");
		Require(asset.tracks[2].id == child.id && !asset.tracks[2].parent, "グループ外へ移動した親と順番を更新する");
		Require(TimelineEditOperations::MoveTrack(asset, child.id, group.id, TimelineTrackDropPosition::IntoGroup),
			"並べ替え後もグループへ移動できる");
		Require(TimelineEditOperations::MoveTrack(asset, group.id, third.id, TimelineTrackDropPosition::After),
			"子を持つグループも下へ移動できる");
		const auto movedChild = std::find_if(asset.tracks.begin(), asset.tracks.end(),
			[&](const auto& track) { return track.id == child.id; });
		Require(asset.tracks.back().id == group.id && movedChild != asset.tracks.end() && movedChild->parent == group.id,
			"グループ移動で子の所属を失わない");
		asset.tracks.back().locked = true;
		const auto before = nlohmann::json(asset);
		Require(!TimelineEditOperations::MoveTrack(asset, second.id, group.id, TimelineTrackDropPosition::After),
			"ロックした移動先へ並べ替えない");
		Require(nlohmann::json(asset) == before, "拒否した並べ替えでデータを変更しない");
	}

	void CheckHierarchyAndDuration() {

		auto asset = MakeAsset();
		auto group = TimelineTrack{};
		group.id = Engine::UUID::New(); group.kind = TimelineTrackKind::Group;
		auto child = group; child.id = Engine::UUID::New(); child.parent = group.id;
		asset.tracks.push_back(group); asset.tracks.push_back(child);
		Require(!TimelineEditOperations::MoveTrack(asset, group.id, child.id, TimelineTrackDropPosition::IntoGroup),
			"子孫への移動で循環を作らない");
		Require(TimelineEditOperations::MoveTrack(asset, asset.tracks[0].id, group.id, TimelineTrackDropPosition::IntoGroup),
			"グループへトラックを移せる");
		Require(asset.tracks[1].parent == group.id, "移動後の親グループが保存される");
		asset.tracks[1].clips.push_back(MakeClip(2, 3));
		TimelineEditOperations::UpdateDuration(asset);
		Require(asset.duration == 20, "固定長はクリップ編集で延長しない");
		asset.durationMode = TimelineDurationMode::BasedOnClips;
		TimelineEditOperations::UpdateDuration(asset);
		Require(asset.duration == 5, "クリップ末尾へ長さを合わせる");
	}
}

bool NEMTests::TestTimelineEditor() {

	try {
		CheckMoveAndTrim();
		CheckRippleAndReplace();
		CheckSplitAndSnap();
		CheckTrackReordering();
		CheckHierarchyAndDuration();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "TimelineEditor: " << error.what() << '\n';
		return false;
	}
}
