#include "AnimationClipTool.h"
#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// imgui
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>

#include <imgui.h>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipTool classMethods
//============================================================================

void AnimationClipTool::OpenEditorTool() {

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		// Window外でPreviewが残った場合も、次フレームで元の値へ戻す
		session_.EndPreviewAndRestore(context);
		return;
	}

	if (!ImGui::Begin("アニメーションクリップ作成ツール", &openWindow_)) {
		ImGui::End();
		// 折りたたみ中は操作できないため、Preview状態だけは必ず解放する
		session_.EndPreviewAndRestore(context);
		return;
	}

	// マニピュレータ/インスペクタでの編集があったフレームだけ検知する、毎フレームの差分比較は避ける
	// ギズモ操作中(useSceneGizmo)か、Undo/Redoでコマンド数が変わったフレームだけ走らせる
	session_.UpdateExternalEdits(context);

	//============================================================================
	//	AnimationClip編集UI
	//============================================================================
	if (ImGui::BeginTable("AnimationClipToolTopLayout", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Toolbar", ImGuiTableColumnFlags_WidthFixed, 420.0f);
		ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextColumn();
		// アセット、編集設定UI
		DrawToolbarUI(context);

		ImGui::TableNextColumn();
		// プロパティ設定UI
		DrawPropertyTreeUI(context);

		ImGui::EndTable();
	}

	ImGui::Separator();

	if (ImGui::BeginTable("AnimationClipToolEditLayout", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Curve", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("KeyInspector", ImGuiTableColumnFlags_WidthFixed, 340.0f);

		ImGui::TableNextColumn();
		// プロパティカーブ編集UI
		DrawCurveEditorUI(context);

		ImGui::TableNextColumn();
		DrawKeyInspectorUI(context);
		DrawGeneratorUI(context);
		DrawEventListUI(context);

		ImGui::EndTable();
	}

	// 再生中、設定されたターゲットエンティティにアニメーションを直で適用する
	session_.UpdatePreviewPlayback(context);

	ImGui::End();

	if (!openWindow_) {
		session_.EndPreviewAndRestore(context);
	}
}

void AnimationClipTool::DrawToolbarUI(const EditorToolContext& context) {

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	//============================================================================
	//	アニメアセットの設定
	//============================================================================
	DrawClipAssetUI(context);

	//============================================================================
	//	再生・編集設定
	//============================================================================
	DrawEditAssetUI(context);

	ImGui::SetWindowFontScale(beforeFontScale);
}

void AnimationClipTool::DrawClipAssetUI(const EditorToolContext& context) {

	AssetID before = session_.GetClipAssetID();

	//============================================================================
	//	アセット設定・保存
	//============================================================================
	{
		MyGUI::BeginPropertyRow("アニメクリップのセット");

		// アニメーションクリップアセットのセット
		ValueEditResult result = MyGUI::AssetReferenceField("",
			session_.GetClipAssetID(), context.toolContext.assetDatabase, { AssetType::AnimationClip },
			{ .useAutoPropertyRow = false,.buttonSize = ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, ImGui::GetFrameHeight()) });
		ImGui::SameLine();
		// 保存ボタン
		if (ImGui::Button("保存", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {

			session_.SaveClipToSelectedAsset(context);
		}
		MyGUI::EndPropertyRow();

		// アセットファイルが変更されたとき
		if (result.valueChanged || before != session_.GetClipAssetID()) {

			// Clipを差し替える前に、前のClipで適用していたPreview値を必ず戻す
			session_.EndPreviewAndRestore(context);
			session_.LoadClipFromSelectedAsset(context);
		}

		if (!session_.GetClipErrorText().empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s", session_.GetClipErrorText().c_str());
			ImGui::PopStyleColor();
		}
	}
	//============================================================================
	//	アニメ対象エンティティのセット
	//============================================================================
	{
		ECSWorld* world = context.GetWorld();
		UUID nextTargetUUID = session_.GetTargetEntityUUID();

		MyGUI::BeginPropertyRow("対象エンティティのセット");

		const float clearButtonWidth = 96.0f;
		const float entityFieldWidth = (std::max)(
			ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
		const ValueEditResult result = MyGUI::EntityReferenceField("", nextTargetUUID, world,
			{ .useAutoPropertyRow = false,.buttonSize = ImVec2(entityFieldWidth, ImGui::GetFrameHeight()) });
		if (result.valueChanged && nextTargetUUID != session_.GetTargetEntityUUID()) {
			session_.SetPreviewTarget(context, nextTargetUUID);
		}
		ImGui::SameLine();
		const bool hasTargetEntity = session_.GetTargetEntityUUID() != UUID{};
		if (!hasTargetEntity) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("解除", ImVec2(clearButtonWidth, ImGui::GetFrameHeight()))) {
			session_.ClearPreviewTarget(context);
		}
		if (!hasTargetEntity) {
			ImGui::EndDisabled();
		}
		MyGUI::EndPropertyRow();
	}
	//============================================================================
	//	アニメーションの再生設定
	//============================================================================
	// アセットが設定されていなければ処理しない
	if (!session_.GetHasClip()) {
		return;
	}
	{
		// アニメーションを再生する長さ
		float duration = session_.GetClip().duration;
		auto result = MyGUI::DragFloat("再生時間", duration, { .dragSpeed = 0.01f,.minValue = 0.01f,
			.maxValue = 10000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
		if (result.valueChanged) {
			// 必ず0.0f以上に制限する
			session_.GetClip().duration = (std::max)(duration, 0.01f);
			session_.GetPreviewTime() = (std::clamp)(session_.GetPreviewTime(), 0.0f, session_.GetClip().duration);
			session_.GetCurveState().visibleTimeMax = (std::max)(session_.GetCurveState().visibleTimeMax, session_.GetClip().duration);
			session_.MarkClipDirty();
		}
		if (MyGUI::Checkbox("最後のキーを再生時間に設定", session_.GetClip().autoDuration)) {

			UpdateAnimationClipAutoDuration(session_.GetClip());
			session_.GetPreviewTime() = (std::clamp)(session_.GetPreviewTime(), 0.0f, AnimationClipEvaluator::GetPlaybackDuration(session_.GetClip()));
			session_.MarkClipDirty();
		}

		// 再生開始時の向きを正面として位置/回転を相対適用する、アクション制作向け
		if (MyGUI::Checkbox("向き相対(位置/回転)", session_.GetClip().relativeTransform)) {
			// 適用方法が変わるので、現在のPreview値を一度戻してから捕捉し直す
			session_.EndPreviewAndRestore(context);
			session_.MarkClipDirty();
		}

		ImGui::SeparatorText("ループ再生についての設定");

		if (MyGUI::Checkbox("ループ再生", session_.GetClip().loop)) {
			session_.MarkClipDirty();
		}
		// ループ再生する場合のみの設定
		if (session_.GetClip().loop) {

			bool bridgeEnabled = session_.GetClip().loopBridge.enabled;
			if (MyGUI::Checkbox("ループのつなぎ補間", bridgeEnabled)) {
				session_.GetClip().loopBridge.enabled = bridgeEnabled;
				session_.MarkClipDirty();
			}
			if (session_.GetClip().loopBridge.enabled) {

				result = {};
				result = MyGUI::DragFloat("補間時間", session_.GetClip().loopBridge.duration, { .dragSpeed = 0.001f,.minValue = 0.001f,
					.maxValue = 10.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
				if (result.valueChanged) {
					session_.GetClip().loopBridge.duration = (std::max)(session_.GetClip().loopBridge.duration, 0.001f);
					session_.MarkClipDirty();
				}
				result = {};
				result = MyGUI::EnumCombo<CurveInterpolationMode>("補間方法", session_.GetClip().loopBridge.interpolation,
					{ .reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
				if (result.valueChanged) {
					session_.MarkClipDirty();
				}
			}
		}
	}
}

void Engine::AnimationClipTool::DrawEditAssetUI(const EditorToolContext& context) {

	ImGui::SeparatorText("アニメ編集設定");

	MyGUI::EnumCombo("編集次元の設定", session_.GetEditDimension(), { .reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });

	if (!session_.GetHasClip()) {
		return;
	}

	auto result = MyGUI::DragFloat("現在の時間", session_.GetPreviewTime(), { .dragSpeed = 0.01f,.minValue = 0.0f,
		.maxValue = session_.GetClip().duration,.closeOnProperty = false,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
	if (result.valueChanged) {

		// Scrub中もSceneViewへ即反映し、カーブ編集結果を確認できるようにする
		session_.GetPreviewTime() = (std::clamp)(session_.GetPreviewTime(), 0.0f, session_.GetClip().duration);
		session_.GetCurveState().currentTime = session_.GetPreviewTime();
		session_.ApplyPreviewAtCurrentTime(context, true);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.3f / %.3f", session_.GetPreviewTime(), session_.GetClip().duration);

	MyGUI::EndPropertyRow();

	MyGUI::DragFloat("再生速度", session_.GetPreviewSpeed(), { .dragSpeed = 0.01f,.minValue = 0.01f,
	.maxValue = 8.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });

	// 再生/ポーズボタン
	if (ImGui::Button(session_.GetPreviewPlaying() ? "ポーズ" : "再生", ImVec2(100.f, ImGui::GetFrameHeight()))) {
		session_.TogglePreviewPlayback(context);
	}
	ImGui::SameLine();
	// 停止ボタン
	if (ImGui::Button("停止", ImVec2(100.f, ImGui::GetFrameHeight()))) {
		session_.StopPreviewPlayback(context);
	}
}

void AnimationClipTool::DrawPropertyTreeUI(const EditorToolContext& context) {

	if (!session_.GetHasClip()) {
		return;
	}

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	ECSWorld* world = context.GetWorld();
	const Entity targetEntity = session_.GetTargetEntity(context);
	const AnimationClipEditDimension effectiveDimension = session_.GetEffectiveEditDimension(context);

	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::BeginDisabled();
	}

	if (ImGui::Button("プロパティ追加", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
		ImGui::OpenPopup("AnimationClipAddProperty");
	}
	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::EndDisabled();
	}
	// 追加プロパティ選択のポップアップ
	if (ImGui::BeginPopup("AnimationClipAddProperty")) {
		if (!world || !world->IsAlive(targetEntity)) {
			ImGui::TextDisabled("アニメ対象のエンティティが設定されていません");
		} else {
			// reflection駆動で個別マテリアルパラメータも動的に列挙するためコンテキストを渡す
			AnimationPropertyQueryContext queryContext{};
			queryContext.assetDatabase = context.toolContext.assetDatabase;
			queryContext.renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
			const std::vector<AnimationPropertyDescriptor> collectedProperties =
				AnimationPropertyRegistry::GetInstance().CollectProperties(*world, targetEntity, queryContext);

			std::unordered_map<std::string, std::vector<const AnimationPropertyDescriptor*>> groups{};
			for (const AnimationPropertyDescriptor& desc : collectedProperties) {
				if (desc.componentName == "Transform") {
					// 2D/3D表示モードに合わないTransform Propertyは追加候補から外す
					if (effectiveDimension == AnimationClipEditDimension::Mode2D && Is3DTransformProperty(desc.propertyPath)) {
						continue;
					}
					if (effectiveDimension == AnimationClipEditDimension::Mode3D && Is2DTransformProperty(desc.propertyPath)) {
						continue;
					}
				}
				groups[desc.componentName].emplace_back(&desc);
			}
			for (auto& [componentName, properties] : groups) {
				if (!ImGui::BeginMenu(componentName.c_str())) {
					continue;
				}
				for (const AnimationPropertyDescriptor* desc : properties) {

					// 同じClip内に同一Property Trackを複数作らないようにする
					const bool exists = std::any_of(session_.GetClip().curveTracks.begin(), session_.GetClip().curveTracks.end(),
						[desc](const AnimationCurveTrack& track) {
							return track.binding.componentName == desc->componentName &&
								track.binding.propertyPath == desc->propertyPath;
						});
					if (exists) {
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(desc->displayName.c_str())) {
						session_.AddPropertyTrack(*desc, *world, targetEntity);
						session_.ApplyPreviewAtCurrentTime(context, true);
					}
					if (exists) {
						ImGui::EndDisabled();
					}
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}

	if (session_.GetClip().curveTracks.empty()) {
		ImGui::TextDisabled("アニメプロパティ無し");
		ImGui::SetWindowFontScale(beforeFontScale);
		return;
	}

	session_.NormalizeSelectedTrackIndex();

	for (size_t i = 0; i < session_.GetClip().curveTracks.size();) {

		AnimationCurveTrack& track = session_.GetClip().curveTracks[i];
		std::optional<AnimationPropertyDescriptor> desc;
		if (world && world->IsAlive(targetEntity)) {
			desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				*world, targetEntity, track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
		}
		const bool missing = !world || !world->IsAlive(targetEntity) ||
			!desc || !desc->hasComponent || !desc->hasComponent(*world, targetEntity);

		ImGui::PushID(static_cast<int>(i));
		const bool selected = session_.GetSelectedTrackIndex() == static_cast<int>(i);

		if (missing) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		}
		const std::string label = BuildTrackLabel(track, world, targetEntity);

		// 適用コンボ + (順番コンボ) + 削除ボタンの幅を先に確保し、ラベルは残りに詰めて確実に収める
		const ImGuiStyle& style = ImGui::GetStyle();
		const bool isQuaternionTrack = track.binding.valueType == AnimationValueType::Quaternion;
		const bool showOrderCombo = isQuaternionTrack && track.applyMode == AnimationApplyMode::Multiply;
		constexpr float kApplyComboWidth = 90.0f;
		constexpr float kOrderComboWidth = 120.0f;
		const float applyLabelWidth = ImGui::CalcTextSize("適用").x + style.ItemInnerSpacing.x;
		const float deleteWidth = ImGui::CalcTextSize("削除").x + style.FramePadding.x * 2.0f;
		float controlsWidth = kApplyComboWidth + applyLabelWidth + style.ItemSpacing.x + deleteWidth;
		if (showOrderCombo) {
			controlsWidth += kOrderComboWidth + style.ItemSpacing.x;
		}
		const float rowWidth = (std::max)(30.0f, ImGui::GetContentRegionAvail().x - controlsWidth - style.ItemSpacing.x);
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(rowWidth, 0.0f))) {
			session_.StoreSelectedTrackEditorView();
			session_.GetSelectedTrackIndex() = static_cast<int>(i);
			session_.LoadSelectedTrackEditorView();
			session_.GetCurveState().ClearSelection();
			session_.GetCurveState().frameSelectionRequest = true;
		}
		if (missing) {
			ImGui::PopStyleColor();
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted("Target Entity does not have this property.");
				ImGui::EndTooltip();
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(kApplyComboWidth);
		if (isQuaternionTrack) {
			constexpr std::array<int, 2> kQuaternionApplyValues{
				static_cast<int>(AnimationApplyMode::Override),
				static_cast<int>(AnimationApplyMode::Multiply),
			};
			if (ImGui::BeginCombo("適用", ApplyModeLabel(track.applyMode))) {
				for (int value : kQuaternionApplyValues) {
					const AnimationApplyMode applyMode = static_cast<AnimationApplyMode>(value);
					const bool isSelected = track.applyMode == applyMode;
					if (ImGui::Selectable(ApplyModeLabel(applyMode), isSelected)) {
						track.applyMode = applyMode;
						session_.MarkClipDirty();
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			// 積の順番はMultiply時だけ意味を持つので、その時だけ式で選ばせる
			if (showOrderCombo) {

				const auto orderFormula = [](QuaternionMultiplyOrder order) {
					return order == QuaternionMultiplyOrder::BaseThenCurve ? "base * curve" : "curve * base";
					};
				ImGui::SameLine();
				ImGui::SetNextItemWidth(kOrderComboWidth);
				if (ImGui::BeginCombo("##Order", orderFormula(track.quaternionMultiplyOrder))) {
					for (QuaternionMultiplyOrder order : { QuaternionMultiplyOrder::BaseThenCurve, QuaternionMultiplyOrder::CurveThenBase }) {
						const bool isSelected = track.quaternionMultiplyOrder == order;
						if (ImGui::Selectable(orderFormula(order), isSelected)) {
							track.quaternionMultiplyOrder = order;
							session_.MarkClipDirty();
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
		} else if (ImGui::BeginCombo("適用", ApplyModeLabel(track.applyMode))) {
			for (AnimationApplyMode applyMode : { AnimationApplyMode::Override,
				AnimationApplyMode::Add, AnimationApplyMode::Multiply }) {
				const bool isSelected = track.applyMode == applyMode;
				if (ImGui::Selectable(ApplyModeLabel(applyMode), isSelected)) {
					track.applyMode = applyMode;
					session_.MarkClipDirty();
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			// プレビュー中は削除するpropertyだけ元のシーン値へ戻し、残りの再生は止めない
			session_.RestoreAndDropPreviewBaseValue(context, track.binding);
			session_.GetClip().curveTracks.erase(session_.GetClip().curveTracks.begin() + i);
			session_.GetCurveState().ClearSelection();
			if (session_.GetSelectedTrackIndex() == static_cast<int>(i)) {
				session_.GetSelectedTrackIndex() = session_.GetClip().curveTracks.empty() ? -1 :
					(std::min)(static_cast<int>(i), static_cast<int>(session_.GetClip().curveTracks.size()) - 1);
			} else if (static_cast<int>(i) < session_.GetSelectedTrackIndex()) {
				--session_.GetSelectedTrackIndex();
			}
			session_.MarkClipDirty();
			// 残ったtrackを現在時刻で反映し直す、プレビューは継続する
			if (session_.GetPreviewActive()) {
				session_.ApplyPreviewAtCurrentTime(context, true);
			}
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++i;
	}
	ImGui::SetWindowFontScale(beforeFontScale);
}

void AnimationClipTool::DrawCurveEditorUI(const EditorToolContext& context) {

	if (!session_.GetHasClip()) {
		return;
	}

	session_.NormalizeSelectedTrackIndex();

	if (session_.GetSelectedTrackIndex() < 0) {
		ImGui::TextDisabled("Select property track.");
		return;
	}

	AnimationCurveTrack& track = session_.GetClip().curveTracks[static_cast<size_t>(session_.GetSelectedTrackIndex())];
	if (session_.GetEditorViewTrackIndex() != session_.GetSelectedTrackIndex()) {
		session_.LoadSelectedTrackEditorView();
	}

	if (track.channels.empty()) {
		ImGui::TextDisabled("Visible curve track is empty.");
		return;
	}

	session_.GetCurveState().visibleTimeMax = (std::max)(session_.GetCurveState().visibleTimeMax, session_.GetClip().duration);
	session_.GetCurveState().currentTime = session_.GetPreviewTime();

	const float previousTime = session_.GetCurveState().currentTime;
	CurveEditSetting curveSetting{};
	curveSetting.size = ImVec2(0.0f, 360.0f);
	curveSetting.autoFit = false;
	curveSetting.showSidePanels = false;
	curveSetting.snap = true;
	curveSetting.snapInterval = 0.001f;
	CurveEditResult result{};
	// 値型ごとにMyGUIのCurve型へ詰め替え、選択TrackだけをEditorに渡す
	switch (track.binding.valueType) {
	case AnimationValueType::Float: {
		CurveFloat curve{};
		curve.channel = track.channels[0];
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session_.GetCurveState(), curveSetting);
		track.channels[0] = curve.channel;
		break;
	}
	case AnimationValueType::Vector3: {
		if (track.channels.size() == 3) {
			CurveVector3 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session_.GetCurveState(), curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Color3: {
		if (track.channels.size() == 3) {
			CurveColor3 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session_.GetCurveState(), curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Color4: {
		if (track.channels.size() == 4) {
			CurveColor4 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session_.GetCurveState(), curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Quaternion: {
		// QuaternionはAxis/Angleとして編集し、AxisとAngleのキーを別々に保持する
		const bool wasAxisAngleTrack = IsQuaternionAxisAngleTrack(track);
		CurveQuaternion curve = BuildQuaternionEditorCurve(track);
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, session_.GetCurveState(), curveSetting);
		if (!wasAxisAngleTrack || result.valueChanged || result.editFinished) {
			StoreQuaternionEditorCurve(curve, track);
			result.valueChanged |= !wasAxisAngleTrack;
		}
		break;
	}
	case AnimationValueType::Vector2:
	default: {
		std::vector<CurveChannelRef> channelRefs{};
		for (CurveChannel& channel : track.channels) {
			CurveChannelRef ref{};
			ref.channel = &channel;
			ref.displayName = channel.name;
			channelRefs.emplace_back(std::move(ref));
		}
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", channelRefs, session_.GetCurveState(), curveSetting);
		break;
	}
	}

	// Color系はカーブだけでは色変化が分かりにくいので、可視時間範囲の色遷移を帯で表示する
	const bool isColorTrack = track.binding.valueType == AnimationValueType::Color3 ||
		track.binding.valueType == AnimationValueType::Color4;
	if (isColorTrack && track.channels.size() >= 3) {

		const bool hasAlpha = track.binding.valueType == AnimationValueType::Color4;
		MyGUI::CurveColorGradientBar(track.channels,
			session_.GetCurveState().visibleTimeMin, session_.GetCurveState().visibleTimeMax, hasAlpha);
	}

	session_.StoreSelectedTrackEditorView();

	if (result.valueChanged || result.editFinished) {
		// キー編集で終端時刻が変わるため、Auto Durationをここで再計算する
		UpdateAnimationClipAutoDuration(session_.GetClip());
		session_.MarkClipDirty();
	}
	if (previousTime != session_.GetCurveState().currentTime || result.valueChanged) {
		// CurveEditor上の時刻移動もToolbarのTimeと同じPreview時刻として扱う
		session_.GetPreviewTime() = (std::clamp)(session_.GetCurveState().currentTime, 0.0f, session_.GetClip().duration);
		session_.GetCurveState().currentTime = session_.GetPreviewTime();
		session_.ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipTool::DrawKeyInspectorUI(const EditorToolContext& context) {

	if (session_.GetSelectedTrackIndex() < 0 || static_cast<int>(session_.GetClip().curveTracks.size()) <= session_.GetSelectedTrackIndex()) {
		return;
	}
	if (!MyGUI::CollapsingHeader("キーインスペクター")) {
		return;
	}
	if (session_.GetCurveState().selectedKeys.empty()) {
		ImGui::TextDisabled("キーが選択されていません");
		return;
	}

	AnimationCurveTrack& track = session_.GetClip().curveTracks[static_cast<size_t>(session_.GetSelectedTrackIndex())];
	CurveKeySelection selection = session_.GetCurveState().selectedKeys.front();
	if (track.channels.size() <= selection.channelIndex ||
		track.channels[selection.channelIndex].keys.size() <= selection.keyIndex) {
		return;
	}

	CurveChannel& channel = track.channels[selection.channelIndex];
	CurveKey& key = channel.keys[selection.keyIndex];

	ImGui::Text("チャンネル: %s", channel.name.c_str());
	bool changed = false;
	float time = key.time;
	if (MyGUI::DragFloat("キー時間", time, { .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 10000.0f }).valueChanged) {
		key.time = (std::max)(0.0f, time);
		changed = true;
	}

	if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {

		CurveQuaternionAxisKey& axisKey = GetQuaternionAxisKeyForEdit(track, selection.keyIndex);
		if (MyGUI::Checkbox("カスタム軸", axisKey.useCustomAxis)) {
			changed = true;
		}
		if (axisKey.useCustomAxis) {
			Vector3 customAxis = axisKey.customAxis;
			if (MyGUI::DragVector3("キー回転軸", customAxis, { .dragSpeed = 0.001f,.minValue = -1.0f,.maxValue = 1.0f }).valueChanged) {
				axisKey.customAxis = customAxis;
				changed = true;
			}
		} else {
			if (axisKey.axes.empty()) {
				axisKey.axes.emplace_back(Axis::X);
			}
			Axis axis = axisKey.axes.front();
			if (MyGUI::EnumCombo("キー回転軸", axis).valueChanged) {
				axisKey.axes = { axis };
				changed = true;
			}
		}
		key.value = GetPrimaryAxisValue(axisKey);
		key.interpolation = CurveInterpolationMode::Constant;
	} else if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 1u) {

		if (MyGUI::DragFloat("キー角度", key.value, { .dragSpeed = 0.1f,.minValue = -36000.0f,.maxValue = 36000.0f, }).valueChanged) {
			changed = true;
		}
	} else if (CanDrawColorRgbKeyEditor(track, selection.channelIndex)) {
		if (DrawColorKeyValueEditor(track, selection.channelIndex, key.time)) {
			changed = true;
		}
	} else {
		if (MyGUI::DragFloat("キー値", key.value, { .dragSpeed = 0.001f,.minValue = -100000.0f,.maxValue = 100000.0f }).valueChanged) {
			changed = true;
		}
	}

	const bool quaternionTrack = track.binding.valueType == AnimationValueType::Quaternion;
	CurveInterpolationMode interpolation = key.interpolation;
	if (!quaternionTrack && interpolation == CurveInterpolationMode::Squad) {
		// SquadはQuaternion専用なので、通常ChannelではSplineとして表示する
		interpolation = CurveInterpolationMode::Spline;
	}

	if (!(IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) && MyGUI::BeginPropertyRow("補間方法")) {
		if (quaternionTrack) {

			if (Engine::ImGuiUtility::EnumCombo<CurveInterpolationMode>("##Value", &interpolation)) {
				key.interpolation = interpolation;
				changed = true;
			}
		} else {

			constexpr std::array<CurveInterpolationMode, 4> kNonQuaternionInterpolations{
				CurveInterpolationMode::Constant,
				CurveInterpolationMode::Linear,
				CurveInterpolationMode::Bezier,
				CurveInterpolationMode::Spline,
			};

			if (ImGui::BeginCombo("##Value", EnumAdapter<CurveInterpolationMode>::ToString(interpolation))) {
				for (CurveInterpolationMode mode : kNonQuaternionInterpolations) {

					const bool selected = interpolation == mode;
					if (ImGui::Selectable(EnumAdapter<CurveInterpolationMode>::ToString(mode), selected)) {
						key.interpolation = mode;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		MyGUI::EndPropertyRow();
	}

	if (key.interpolation == CurveInterpolationMode::Bezier) {
		Vector2 inTangent = key.inTangent;
		if (MyGUI::DragVector2("入力タンジェント", inTangent, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f }).valueChanged) {
			key.inTangent = inTangent;
			changed = true;
		}
		Vector2 outTangent = key.outTangent;
		if (MyGUI::DragVector2("出力タンジェント", outTangent, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f }).valueChanged) {
			key.outTangent = outTangent;
			changed = true;
		}
	}

	if (changed) {
		const float editedTime = key.time;
		if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {
			SortQuaternionAxisKeys(track);
		} else {
			channel.SortKeys();
		}
		// 時刻変更で並びが変わるため、編集していたKeyを再選択する
		for (uint32_t i = 0; i < channel.keys.size(); ++i) {
			if (std::abs(channel.keys[i].time - editedTime) <= 0.0005f) {
				session_.GetCurveState().SelectSingle(selection.channelIndex, i);
				break;
			}
		}
		UpdateAnimationClipAutoDuration(session_.GetClip());
		session_.MarkClipDirty();
		session_.ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipTool::DrawGeneratorUI(const EditorToolContext& context) {

	if (session_.GetSelectedTrackIndex() < 0 || static_cast<int>(session_.GetClip().curveTracks.size()) <= session_.GetSelectedTrackIndex()) {
		return;
	}

	AnimationCurveTrack& track = session_.GetClip().curveTracks[static_cast<size_t>(session_.GetSelectedTrackIndex())];
	if (track.channels.empty()) {
		return;
	}

	if (!MyGUI::CollapsingHeader("カーブ生成")) {
		return;
	}

	// 生成条件と適用先のUIは共通実装を使う
	const std::vector<CurveBakeTarget> bakeTargets = BuildBakeTargets(track);
	if (DrawCurveGenerator(session_.GetGeneratorState(), track.channels, bakeTargets)) {
		session_.UpdateAutoDurationAndPreview(context);
	}
}

void AnimationClipTool::DrawEventListUI([[maybe_unused]] const EditorToolContext& context) {

	if (!session_.GetHasClip()) {
		return;
	}
	if (!MyGUI::CollapsingHeader("イベント")) {
		return;
	}

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(session_.GetClip().events.size()); ++i) {

		ImGui::PushID(i);
		AnimationEvent& event = session_.GetClip().events[static_cast<size_t>(i)];
		if (ImGui::TreeNodeEx("Event", ImGuiTreeNodeFlags_DefaultOpen, "イベント : %s",
			event.name.empty() ? "<名前なし>" : event.name.c_str())) {

			if (MyGUI::InputText("名前", event.name).valueChanged) {
				session_.MarkClipDirty();
			}
			if (MyGUI::DragFloat("時刻", event.time, { .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 10000.0f }).valueChanged) {
				event.time = std::clamp(event.time, 0.0f, session_.GetClip().duration);
				session_.MarkClipDirty();
			}
			if (MyGUI::DragFloat("floatパラメータ", event.floatParam, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f }).valueChanged) {
				session_.MarkClipDirty();
			}
			if (MyGUI::DragInt("intパラメータ", event.intParam, { .dragSpeed = 1.0f,.minValue = -1000000,.maxValue = 1000000 }).valueChanged) {
				session_.MarkClipDirty();
			}
			if (MyGUI::InputText("stringパラメータ", event.stringParam).valueChanged) {
				session_.MarkClipDirty();
			}
			if (ImGui::Button("イベントを削除", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				removeIndex = i;
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (0 <= removeIndex && removeIndex < static_cast<int>(session_.GetClip().events.size())) {
		session_.GetClip().events.erase(session_.GetClip().events.begin() + removeIndex);
		session_.MarkClipDirty();
	}
	if (ImGui::Button("イベントを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		// 現在のプレビュー再生位置にイベントを追加する
		AnimationEvent event{};
		event.time = std::clamp(session_.GetPreviewTime(), 0.0f, session_.GetClip().duration);
		event.name = "Event";
		session_.GetClip().events.emplace_back(std::move(event));
		session_.MarkClipDirty();
	}
}
