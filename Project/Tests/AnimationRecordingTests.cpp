#include "AnimationRecordingTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Animation/AnimationClipDocument.h>
#include <Engine/Editor/Animation/AnimationPropertyEditCapture.h>
#include <Engine/Editor/Animation/AnimationKeyRecording.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditSession.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/ImGui/ImGuiCurveEditorInternal.h>

// c++
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool value, const char* message) {

		if (!value) throw std::runtime_error(message);
	}

	// 全対応型の開始キーと同時刻更新を確認する
	void CheckValueTypes() {

		const std::array<AnimationValueType, 8> types{AnimationValueType::Float, AnimationValueType::Int,
			AnimationValueType::Vector2, AnimationValueType::Vector3, AnimationValueType::Vector4,
			AnimationValueType::Color3, AnimationValueType::Color4, AnimationValueType::Quaternion};
		const std::array<AnimationPropertyValue, 8> before{0.0f, int32_t{0}, Vector2(0, 0), Vector3(0, 0, 0),
			Vector4(0, 0, 0, 0), Color3(0, 0, 0), Color4(0, 0, 0, 0), Quaternion::Identity()};
		const std::array<AnimationPropertyValue, 8> after{2.0f, int32_t{3}, Vector2(2, 3), Vector3(2, 3, 4),
			Vector4(2, 3, 4, 5), Color3(0.2f, 0.3f, 0.4f), Color4(0.2f, 0.3f, 0.4f, 0.5f),
			Quaternion::FromEulerDegrees({25, 70, -15})};
		for (size_t i = 0; i < types.size(); ++i) {
			AnimationClipAsset clip;
			const std::array changes{RecordedAnimationProperty{{"Test", "value", types[i]}, before[i], after[i]}};
			Require(RecordAnimationProperties(clip, changes, 0.5, 60.0), "型ごとの編集を記録する");
			Require(clip.curveTracks.size() == 1, "変更したプロパティだけを記録する");
			auto& track = clip.curveTracks.front();
			AnimationPropertyValue value;
			Require(AnimationClipEvaluator::EvaluateTrack(track, 0.0f, value) &&
				AnimationClipEditorUtility::ApproxEqualValue(value, before[i]), "最初の編集が途中でも開始値を保持する");
			Require(AnimationClipEvaluator::EvaluateTrack(track, 0.5f, value) &&
				AnimationClipEditorUtility::ApproxEqualValue(value, after[i]), "記録した時刻の値を取得する");
			const auto count = track.channels.front().keys.size();
			Require(RecordAnimationProperties(clip, changes, 0.5, 60.0) &&
				track.channels.front().keys.size() == count, "同じ時刻のキーを重複しない");
		}
	}

	// オイラー編集を維持してQuaternionを記録する
	void CheckEulerRecording() {

		AnimationClipAsset clip;
		const std::array changes{RecordedAnimationProperty{{"Transform", "localRotation", AnimationValueType::Quaternion},
			Quaternion::Identity(), Quaternion::FromEulerDegrees({0, 90, 0})}};
		RecordAnimationProperties(clip, changes, 0.5, 60.0);
		AnimationClipEditorUtility::ConvertQuaternionTrackToEuler(clip.curveTracks.front());
		RecordAnimationProperties(clip, changes, 1.0, 60.0);
		Require(IsQuaternionEulerTrack(clip.curveTracks.front()), "記録中も回転の編集方式を保持する");
		AnimationPropertyValue value;
		Require(AnimationClipEvaluator::EvaluateTrack(clip.curveTracks.front(), 1.0f, value) &&
			AnimationClipEditorUtility::ApproxEqualValue(value, changes.front().after), "オイラーキーも同じ回転を返す");
	}

	// 特異姿勢の前後でも回転を損なわず角度へ変換する
	void CheckEulerRoundTrip() {

		const std::array angles{Vector3(0, 90, 0), Vector3(0, -90, 0), Vector3(25, 90, 40),
			Vector3(-35, -90, 15), Vector3(15, 89.99f, -30), Vector3(15, 90.01f, -30),
			Vector3(10, 75, 170), Vector3(50, -89.999f, 120), Vector3(0, 180, 0)};
		for (const auto& degrees : angles) {
			const auto expected = Quaternion::FromEulerDegrees(degrees);
			auto actual = Quaternion::FromEulerDegrees(Quaternion::ToEulerDegrees(expected));
			if (Quaternion::Dot(expected, actual) < 0.0f) actual = -actual;
			const float error = (std::max)({std::abs(expected.x - actual.x), std::abs(expected.y - actual.y),
				std::abs(expected.z - actual.z), std::abs(expected.w - actual.w)});
			Require(error <= 0.00001f, "Quaternionとオイラー角の往復で特異姿勢の回転を変えない");
		}
	}

	// 初期表示範囲の外にある記録キーも全体表示へ含める
	void CheckRecordedCurveFit() {

		for (const auto range : {Vector2(2, 3), Vector2(-8, -2), Vector2(5, 5)}) {
			CurveFloat curve;
			curve.channel.AddKey(0.0f, range.x);
			curve.channel.AddKey(1.0f, range.y);
			CurveEditorState state;
			CurveEditorUtility::FitView(ImRect(ImVec2(0, 0), ImVec2(600, 200)), GetCurveChannels(curve), state);
			Require(state.visibleValueMin < range.x && state.visibleValueMax > range.y &&
				std::isfinite(state.pixelsPerValue) && state.pixelsPerValue > 0.0f,
				"全体表示で記録値の範囲と上下余白を反映する");
		}
	}

	// 表示された値を加算乗算の素材値へ逆変換する
	void CheckApplicationRecording() {

		for (const auto mode : {AnimationApplyMode::Add, AnimationApplyMode::Multiply}) {
			AnimationClipAsset clip;
			AnimationCurveTrack track;
			track.binding = {"Test", "value", AnimationValueType::Float};
			track.applyMode = mode;
			AnimationClipEditorUtility::SetupTrackInitialValue(track, 2.0f);
			SetAnimationPropertyKey(track, 0.0f, 2.0f);
			clip.curveTracks.push_back(track);
			const float before = mode == AnimationApplyMode::Add ? 12.0f : 20.0f;
			const float after = mode == AnimationApplyMode::Add ? 15.0f : 50.0f;
			const std::array changes{RecordedAnimationProperty{track.binding, before, after, 10.0f}};
			Require(RecordAnimationProperties(clip, changes, 0.5, 60.0), "加算乗算も表示値からキーへ変換する");
			AnimationPropertyValue value;
			Require(AnimationClipEvaluator::EvaluateTrack(clip.curveTracks.front(), 0.5f, value) &&
				std::get<float>(value) == 5.0f, "基準値を素材へ二重に記録しない");
		}
		AnimationClipAsset clip;
		clip.relativeTransform = true;
		AnimationCurveTrack track;
		track.binding = {"Transform", "localPos", AnimationValueType::Vector3};
		AnimationClipEditorUtility::SetupTrackInitialValue(track, Vector3(0, 0, 0));
		SetAnimationPropertyKey(track, 0.0f, Vector3(0, 0, 0));
		clip.curveTracks.push_back(track);
		const Quaternion origin = Quaternion::FromEulerDegrees({0, 90, 0});
		const Vector3 base(10, 2, 4);
		const auto rotation = Quaternion::MakeRotateMatrix(origin);
		const std::array changes{RecordedAnimationProperty{track.binding,
			base + Vector3::TransferNormal({0, 0, 2}, rotation),
			base + Vector3::TransferNormal({0, 0, 4}, rotation), base, origin}};
		Require(RecordAnimationProperties(clip, changes, 0.5, 60.0), "向き相対の素材にもキーを記録する");
		AnimationPropertyValue value;
		Require(AnimationClipEvaluator::EvaluateTrack(clip.curveTracks.front(), 0.5f, value) &&
			AnimationClipEditorUtility::ApproxEqualValue(value, Vector3(0, 0, 4)), "記録時の位置回転を素材へ焼き込まない");
	}

	// 複数ツールが同じ編集値と履歴を参照する
	void CheckSharedDocument() {

		auto clock = std::make_shared<AnimationEditHistoryClock>();
		auto document = std::make_shared<AnimationClipDocument>();
		document->SetHistoryClock(clock);
		AnimationClipAsset clip;
		clip.name = "Original";
		document->Reset(clip);
		AnimationClipEditSession first, second;
		first.AttachDocument(document);
		second.AttachDocument(document);
		first.GetClip().name = "Edited";
		first.MarkClipDirty();
		first.CommitHistory();
		const auto serial = document->GetUndoSerial();
		Require(second.GetClip().name == "Edited" && second.GetClipDirty(), "別ツールへ編集が即座に反映される");
		Require(serial > 0 && second.RestoreHistory(false) && first.GetClip().name == "Original",
			"共有編集を同じ履歴で取り消す");
		Require(document->GetRedoSerial() == serial && !document->IsDirty(), "開始値へ戻したら未保存状態を解除する");
		Require(first.RestoreHistory(true) && second.GetClip().name == "Edited", "共有編集をやり直す");
		first.RestoreHistory(false);
		auto other = std::make_shared<AnimationClipDocument>();
		other->SetHistoryClock(clock);
		other->Reset(clip);
		other->GetClip().name = "Other";
		other->MarkDirty();
		other->CommitHistory();
		Require(document->GetRedoSerial() == 0 && !document->RestoreHistory(true),
			"別アセットの新規編集後は古いRedoを適用しない");
	}

	struct ProbeRecorder final : IAnimationPropertyRecorder {

		AnimationClipAsset clip;
		int count = 0;

		bool Record(std::span<const RecordedAnimationProperty> changes) override {

			++count;
			return RecordAnimationProperties(clip, changes, 0.5, 60.0);
		}
		bool RestoreHistory([[maybe_unused]] bool redo) override { return false; }
	};

	// シミュレーションを拾わずユーザーの編集区間だけを記録する
	void CheckEditCapture() {

		RegisterBuiltinAnimationProperties();
		ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponent<TransformComponent>(entity);
		auto recorder = std::make_shared<ProbeRecorder>();
		AnimationPropertyEditCapture::SetRecorder(world, entity, recorder, {});
		world.GetComponent<TransformComponent>(entity).localPos.x = 2.0f;
		Require(!AnimationPropertyEditCapture::Commit(world, entity, "Transform") && recorder->count == 0,
			"通常の実行値変化はキーを生成しない");
		AnimationPropertyEditCapture::Begin(world, entity, "Transform");
		AnimationPropertyEditCapture::Begin(world, entity, "Transform");
		Require(AnimationPropertyEditCapture::IsEditing(world), "操作中はTimelineの再評価を保留する");
		world.GetComponent<TransformComponent>(entity).localPos.x = 7.0f;
		Require(AnimationPropertyEditCapture::Commit(world, entity, "Transform") && recorder->count == 1,
			"連続した編集を一回の記録として確定する");
		Require(world.GetComponent<TransformComponent>(entity).localPos.x == 2.0f &&
			!AnimationPropertyEditCapture::IsEditing(world), "Sceneの開始値を書き換えず編集区間を終了する");
		Require(recorder->clip.curveTracks.size() == 1, "Transformの別型エイリアスを重複記録しない");
		AnimationPropertyEditCapture::Begin(world, entity, "Transform");
		AnimationPropertyEditCapture::Cancel(world, entity, "Transform");
		Require(!AnimationPropertyEditCapture::Commit(world, entity, "Transform") && recorder->count == 1,
			"取り消した編集はキーを生成しない");
		AnimationPropertyEditCapture::ClearRecorder(world, recorder.get());
		Require(!AnimationPropertyEditCapture::HasRecorder(world), "記録終了後に登録を残さない");
	}
}

bool NEMTests::TestAnimationRecording() {

	try {
		CheckValueTypes();
		CheckEulerRecording();
		CheckEulerRoundTrip();
		CheckRecordedCurveFit();
		CheckApplicationRecording();
		CheckSharedDocument();
		CheckEditCapture();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "AnimationRecording: " << error.what() << '\n';
		return false;
	}
}
