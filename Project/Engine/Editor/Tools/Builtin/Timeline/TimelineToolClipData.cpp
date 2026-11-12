#include "TimelineTool.h"
#include "TimelineClipFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Animation/Timeline/TimelineClipInstance.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <stdexcept>

using namespace Engine;

bool TimelineTool::ValidateClipData(const TimelineTrack& track, const TimelineClip& clip) {

	const std::string signature = std::to_string(static_cast<int>(track.kind)) + ":" +
		std::to_string(static_cast<int>(clip.easing)) + clip.data.dump();
	if (signature == checkedClipData_) return clipDataError_.empty();
	checkedClipData_ = signature;
	clipDataError_.clear();
	try {
		const auto& data = clip.data;
		if (!data.is_object()) throw std::invalid_argument("クリップの設定はオブジェクトで指定してください");
		if (track.kind <= TimelineTrackKind::TimeScale && track.kind != TimelineTrackKind::Group) {
			if (!CreateBuiltinTimelineClip(track, clip, clipDataError_)) return false;
		}
		for (const char* key : {"debug", "eulerEdit", "loop"}) {
			if (data.contains(key) && !data.at(key).is_boolean()) throw std::invalid_argument(std::string(key) + " はboolで指定してください");
		}
		for (const char* key : {"asset", "timeline"}) {
			if (data.contains(key) && !data.at(key).is_string()) throw std::invalid_argument(std::string(key) + " はアセットGUIDで指定してください");
		}
		if (track.kind == TimelineTrackKind::Event || track.kind == TimelineTrackKind::Window || track.kind == TimelineTrackKind::Custom) {
			for (const char* key : {"name", "stringParam", "typeName"}) {
				if (data.contains(key) && !data.at(key).is_string()) throw std::invalid_argument(std::string(key) + " は文字列で指定してください");
			}
		}
		if (track.kind == TimelineTrackKind::SubTimeline && data.contains("bindings")) {
			if (!data.at("bindings").is_object()) throw std::invalid_argument("子Timelineの割り当て形式が不正です");
			for (const auto& binding : data.at("bindings")) if (!binding.is_string())
				throw std::invalid_argument("子Timelineの割り当て名は文字列で指定してください");
		}
		if (track.kind == TimelineTrackKind::Custom && data.contains("fields") && !data.at("fields").is_object())
			throw std::invalid_argument("独自クリップのフィールド形式が不正です");
	} catch (const std::exception& error) { clipDataError_ = error.what(); }
	return clipDataError_.empty();
}

void TimelineTool::DrawClipData(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip) {

	namespace Field = TimelineClipFields;
	auto& data = clip.data;
	auto* database = context.toolContext.assetDatabase;
	switch (track.kind) {
	case TimelineTrackKind::SkinnedAnimation: {
		std::string selected = data.value("clip", "");
		const Entity target = ResolveTrackTarget(context, track);
		const auto* runtime = context.GetWorld() ? TryGetSkinnedAnimationRuntime(*context.GetWorld(), target) : nullptr;
		if (runtime && !runtime->availableClips.empty()) {
			if (MyGUI::StringCombo("骨アニメーション", selected, runtime->availableClips, "選択してください", true).valueChanged)
				data["clip"] = selected;
		} else Field::Text("骨アニメーション名", data, "clip");
		Field::Bool("素材をループ", data, "loop");
		break;
	}
	case TimelineTrackKind::Motion: DrawMotionClipData(context, clip); break;
	case TimelineTrackKind::Particle:
		Field::Bool("1回だけ発生", data, "oneShot");
		Field::Integer("乱数Seed", data, "randomSeed", 1);
		Field::Bool("区間終了で発生を停止", data, "stopEmittingOnEnd", true);
		Field::Bool("キャンセル時に粒子を消去", data, "clearOnCancel", true);
		MyGUI::Text("同時発生", "同じParticleSystemの区間は重ねず、別の対象かPrefabを使用してください");
		break;
	case TimelineTrackKind::Prefab:
		Field::Asset("Prefab", data, "asset", database, {AssetType::Prefab});
		Field::Binding("追従先", data, "parentBinding", session_.GetAsset());
		Field::Text("ボーン名", data, "joint");
		Field::Vector("位置", data, "offset");
		Field::Vector("回転", data, "rotation");
		Field::Vector("スケール", data, "scale", Vector3(1, 1, 1));
		Field::Bool("区間終了で破棄", data, "destroyOnEnd", true);
		break;
	case TimelineTrackKind::Collision:
		Field::Bool("形状を表示", data, "debug", true);
		MyGUI::Text("区間", "区間中はCollisionを有効、終了時は無効にします");
		MyGUI::Text("ゲーム処理", "命中時の処理はC#の衝突通知で設定できます");
		break;
	case TimelineTrackKind::Camera:
		Field::Binding("ショット用カメラ", data, "shotBinding", session_.GetAsset());
		Field::Number("画角", data, "fov", 60.0f, 1.0f, 179.0f);
		Field::Vector("シェイクの強さ", data, "shake");
		Field::Number("シェイクの周波数", data, "shakeFrequency", 12.0f, 0.0f);
		Field::Integer("乱数Seed", data, "seed", 1);
		break;
	case TimelineTrackKind::Audio:
		Field::Asset("音声", data, "asset", database, {AssetType::Audio});
		Field::Number("音量", data, "volume", 1.0f, 0.0f, 4.0f);
		Field::Number("ピッチ", data, "pitch", 1.0f, 0.01f, 4.0f);
		Field::Number("立体音響の割合", data, "spatialBlend", 0.0f, 0.0f, 1.0f);
		Field::Bool("素材をループ", data, "loop");
		break;
	case TimelineTrackKind::Property: DrawPropertyClipData(context, track, clip); break;
	case TimelineTrackKind::Activation:
		Field::Bool("区間中に有効", data, "active", true);
		break;
	case TimelineTrackKind::TimeScale:
		Field::Number("時間倍率", data, "scale", 0.25f, 0.0f, 100.0f);
		Field::Bool("全体へ適用", data, "global");
		Field::Bool("ヒットストップ", data, "hitStop");
		if (data.value("hitStop", false) || data.value("scale", 0.25f) == 0.0f)
			Field::Number("実時間での長さ", data, "holdSeconds", 0.1f, 0.001f, 60.0f);
		break;
	case TimelineTrackKind::Event:
		Field::Text("通知名", data, "name");
		Field::Number("float値", data, "floatParam");
		Field::Integer("int値", data, "intParam");
		Field::Text("string値", data, "stringParam");
		MyGUI::Text("発生時刻", "クリップの開始位置で通知します");
		break;
	case TimelineTrackKind::Window:
		Field::Text("受付区間名", data, "name");
		MyGUI::Text("C#", "IsWindowOpenで区間を取得できます");
		break;
	case TimelineTrackKind::SubTimeline: {
		Field::Asset("子Timeline", data, "timeline", database, {AssetType::Timeline});
		Field::Bool("子Timelineをループ", data, "loop");
		MyGUI::Text("割り当て", "同じ名前の割り当て先とパラメーターを引き継ぎます");
		const AssetID child = ParseAssetID(data, "timeline");
		const uint64_t revision = database ? database->GetContentRevision(child) : 0;
		if (child != childAsset_ || revision != childRevision_) {
			childAsset_ = child;
			childRevision_ = revision;
			childError_.clear();
			childLoaded_ = database && child && LoadTimelineAsset(database->ResolveFullPath(child), childTimeline_, &childError_);
		}
		if (child == session_.GetAssetID()) MyGUI::Text("子Timeline", "自分自身は設定できません");
		else if (childLoaded_ && MyGUI::CollapsingHeader("子Timelineの割り当て", false)) {
			ImGui::Indent();
			if (!data.contains("bindings") || !data["bindings"].is_object()) data["bindings"] = nlohmann::json::object();
			for (const auto& role : childTimeline_.bindings) {
				ImGui::PushID(ToString(role.id).c_str());
				nlohmann::json mapping{{"role", data["bindings"].value(role.name, role.name)}};
				const std::string label(TimelineEditorFields::BindingLabel(role.name));
				if (Field::Binding(label.c_str(), mapping, "role", session_.GetAsset()).valueChanged) {
					if (mapping.value("role", "").empty()) data["bindings"].erase(role.name);
					else data["bindings"][role.name] = mapping.at("role");
				}
				ImGui::PopID();
			}
			ImGui::Unindent();
		}
		if (!childError_.empty()) MyGUI::Text("子Timeline", childError_);
		break;
	}
	case TimelineTrackKind::Custom: DrawCustomClipData(context, clip); break;
	case TimelineTrackKind::Group: break;
	}
}
