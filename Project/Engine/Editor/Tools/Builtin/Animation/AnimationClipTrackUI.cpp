#include "AnimationClipEditorUI.h"
#include "AnimationClipEditSession.h"
#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>

// c++
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>

// imgui
#include <imgui_internal.h>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipEditorUI
//============================================================================

void Engine::AnimationClipEditorUI::DrawPropertyTreeUI(AnimationClipEditSession& session, const EditorToolContext& context) {

	if (!session.GetHasClip()) {
		return;
	}

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	ECSWorld* world = context.GetWorld();
	const Entity targetEntity = session.GetTargetEntity(context);
	const AnimationClipEditDimension effectiveDimension = session.GetEffectiveEditDimension(context);

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
			// Material Parameterを含めて追加候補を集める
			AnimationPropertyQueryContext queryContext{};
			queryContext.assetDatabase = context.toolContext.assetDatabase;
			queryContext.renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
			const std::vector<AnimationPropertyDescriptor> collectedProperties =
				AnimationPropertyRegistry::GetInstance().CollectProperties(*world, targetEntity, queryContext);

			std::unordered_map<std::string, std::vector<const AnimationPropertyDescriptor*>> groups{};
			for (const AnimationPropertyDescriptor& desc : collectedProperties) {
				if (desc.componentName == "Transform") {
					// 編集次元に合うTransformだけを表示する
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

					// 同じPropertyの重複追加を防ぐ
					const bool exists = std::any_of(session.GetClip().curveTracks.begin(), session.GetClip().curveTracks.end(),
						[desc](const AnimationCurveTrack& track) {
							return track.binding.componentName == desc->componentName &&
								   track.binding.propertyPath == desc->propertyPath;
						});
					if (exists) {
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(desc->displayName.c_str())) {
						session.AddPropertyTrack(*desc, *world, targetEntity);
						session.ApplyPreviewAtCurrentTime(context, true);
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

	if (session.GetClip().curveTracks.empty()) {
		ImGui::TextDisabled("アニメプロパティ無し");
		ImGui::SetWindowFontScale(beforeFontScale);
		return;
	}

	session.NormalizeSelectedTrackIndex();

	for (size_t i = 0; i < session.GetClip().curveTracks.size();) {

		AnimationCurveTrack& track = session.GetClip().curveTracks[i];
		std::optional<AnimationPropertyDescriptor> desc;
		if (world && world->IsAlive(targetEntity)) {
			desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				*world, targetEntity, track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
		}
		const bool missing = !world || !world->IsAlive(targetEntity) || !desc || !desc->hasComponent ||
							 !desc->hasComponent(*world, targetEntity);

		ImGui::PushID(static_cast<int>(i));
		const bool selected = session.GetSelectedTrackIndex() == static_cast<int>(i);

		if (missing) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		}
		const std::string label = BuildTrackLabel(track, world, targetEntity);

		// 操作ボタンの幅を確保してラベルを収める
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
			session.StoreSelectedTrackEditorView();
			session.GetSelectedTrackIndex() = static_cast<int>(i);
			session.LoadSelectedTrackEditorView();
			session.GetCurveState().ClearSelection();
			session.GetCurveState().frameSelectionRequest = true;
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
						session.MarkClipDirty();
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			// Multiply時だけ回転の積順を表示する
			if (showOrderCombo) {

				const auto orderFormula = [](QuaternionMultiplyOrder order) {
					return order == QuaternionMultiplyOrder::BaseThenCurve ? "base * curve" : "curve * base";
				};
				ImGui::SameLine();
				ImGui::SetNextItemWidth(kOrderComboWidth);
				if (ImGui::BeginCombo("##Order", orderFormula(track.quaternionMultiplyOrder))) {
					for (QuaternionMultiplyOrder order :
						{QuaternionMultiplyOrder::BaseThenCurve, QuaternionMultiplyOrder::CurveThenBase}) {
						const bool isSelected = track.quaternionMultiplyOrder == order;
						if (ImGui::Selectable(orderFormula(order), isSelected)) {
							track.quaternionMultiplyOrder = order;
							session.MarkClipDirty();
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
		} else if (ImGui::BeginCombo("適用", ApplyModeLabel(track.applyMode))) {
			for (AnimationApplyMode applyMode :
				{AnimationApplyMode::Override, AnimationApplyMode::Add, AnimationApplyMode::Multiply}) {
				const bool isSelected = track.applyMode == applyMode;
				if (ImGui::Selectable(ApplyModeLabel(applyMode), isSelected)) {
					track.applyMode = applyMode;
					session.MarkClipDirty();
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			// 削除するPropertyだけ元の値へ戻す
			session.RestoreAndDropPreviewBaseValue(context, track.binding);
			session.GetClip().curveTracks.erase(session.GetClip().curveTracks.begin() + i);
			session.GetCurveState().ClearSelection();
			if (session.GetSelectedTrackIndex() == static_cast<int>(i)) {
				session.GetSelectedTrackIndex() =
					session.GetClip().curveTracks.empty()
						? -1
						: (std::min)(static_cast<int>(i), static_cast<int>(session.GetClip().curveTracks.size()) - 1);
			} else if (static_cast<int>(i) < session.GetSelectedTrackIndex()) {
				--session.GetSelectedTrackIndex();
			}
			session.MarkClipDirty();
			// 残ったTrackのプレビューを更新する
			if (session.GetPreviewActive()) {
				session.ApplyPreviewAtCurrentTime(context, true);
			}
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++i;
	}
	ImGui::SetWindowFontScale(beforeFontScale);
}
