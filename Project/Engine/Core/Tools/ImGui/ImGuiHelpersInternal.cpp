#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

// c++
#include <algorithm>
#include <format>
#include <filesystem>
#include <cstring>

namespace {

	// 軸ラベルの表示幅
	constexpr float kAxisLabelWidth = 14.0f;

	// 軸ごとの表示情報
	struct AxisDisplayInfo {
		const char* name;
		ImVec4 color;
	};
	// 軸に対応するラベル名と表示色を取得する
	AxisDisplayInfo GetAxisDisplayInfo(char axis) {
		switch (axis) {
		case 'X': return { "X", ImVec4(1.0f, 0.26f, 0.20f, 1.0f) }; // red
		case 'Y': return { "Y", ImVec4(0.20f, 0.45f, 1.0f, 1.0f) }; // blue
		case 'Z': return { "Z", ImVec4(0.20f, 1.0f, 0.25f, 1.0f) }; // green
		case 'W': return { "W", ImVec4(0.90f, 0.78f, 0.20f, 1.0f) }; // yellow
		default:  return { "-", ImVec4(0.70f, 0.70f, 0.70f, 1.0f) };
		}

	}

} // namespace

//============================================================================
//	Easing functions
//============================================================================
void Easing::SelectEasingType(EasingType& easingType,
	const std::string& label, float itemWidth) {

	const char* easeInOptions[] = {
		"EaseInSine", "EaseInQuad", "EaseInCubic", "EaseInQuart",
		"EaseInQuint", "EaseInExpo", "EaseInCirc", "EaseInBack", "EaseInBounce"
	};
	const char* easeOutOptions[] = {
		"EaseOutSine", "EaseOutQuad", "EaseOutCubic", "EaseOutQuart",
		"EaseOutQuint", "EaseOutExpo", "EaseOutCirc", "EaseOutBack", "EaseOutBounce"
	};
	const char* easeInOutOptions[] = {
		"EaseInOutSine", "EaseInOutQuad", "EaseInOutCubic", "EaseInOutQuart",
		"EaseInOutQuint", "EaseInOutExpo", "EaseInOutCirc", "EaseInOutBounce"
	};

	const int baseIn = static_cast<int>(EasingType::EaseInSine);
	const int baseOut = static_cast<int>(EasingType::EaseOutSine);
	const int baseInOut = static_cast<int>(EasingType::EaseInOutSine);
	const int easingIndex = static_cast<int>(easingType);

	const char* previewLabel = "Linear";
	if (easingIndex >= baseIn && easingIndex < baseOut) {
		previewLabel = easeInOptions[easingIndex - baseIn];
	} else if (easingIndex >= baseOut && easingIndex < baseInOut) {
		previewLabel = easeOutOptions[easingIndex - baseOut];
	} else if (easingIndex >= baseInOut) {
		previewLabel = easeInOutOptions[easingIndex - baseInOut];
	}

	ImGui::SetNextItemWidth(itemWidth);
	if (!ImGui::BeginCombo(("EasingType##" + label).c_str(), previewLabel)) {
		return;
	}

	if (ImGui::Button("Linear", ImVec2(itemWidth, 24.0f))) {
		easingType = EasingType::Linear;
	}

	ImGui::PushItemWidth(itemWidth);
	if (ImGui::BeginCombo("EaseIn", "")) {
		for (int i = 0; i < IM_ARRAYSIZE(easeInOptions); ++i) {
			const bool selected = static_cast<int>(easingType) == baseIn + i;
			if (ImGui::Selectable(easeInOptions[i], selected)) {
				easingType = static_cast<EasingType>(baseIn + i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::BeginCombo("EaseOut", "")) {
		for (int i = 0; i < IM_ARRAYSIZE(easeOutOptions); ++i) {
			const bool selected = static_cast<int>(easingType) == baseOut + i;
			if (ImGui::Selectable(easeOutOptions[i], selected)) {
				easingType = static_cast<EasingType>(baseOut + i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::BeginCombo("EaseInOut", "")) {
		for (int i = 0; i < IM_ARRAYSIZE(easeInOutOptions); ++i) {
			const bool selected = static_cast<int>(easingType) == baseInOut + i;
			if (ImGui::Selectable(easeInOutOptions[i], selected)) {
				easingType = static_cast<EasingType>(baseInOut + i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();
	ImGui::EndCombo();
}

namespace Engine {

	std::string FormatFloat(float value, uint32_t precision) {
		return std::format("{:.{}f}", value, precision);
	}

	std::string MakeAssetDisplayNameFromPath(const std::string& assetPath) {
		const std::filesystem::path path = Engine::Algorithm::PathFromUTF8(assetPath);
		const std::string fileName = Engine::Algorithm::PathToUTF8(path.filename());
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		static constexpr const char* suffixes[] = {
			".scene.json", ".prefab.json", ".material.json", ".shader.json",
			".pipeline.json", ".animclip.json", ".graph.json"
		};
		for (const char* suffix : suffixes) {
			if (Engine::Algorithm::EndsWith(lower, suffix)) {
				return fileName.substr(0, fileName.size() - std::strlen(suffix));
			}
		}
		return fileName;
	}

	ImTextureID ResolveTextureAssetPreview(GraphicsCore* graphicsCore, const AssetDatabase* assetDatabase, AssetID assetID) {
		if (!graphicsCore || !assetDatabase || !assetID) { return ImTextureID{}; }
		const AssetMeta* meta = assetDatabase->Find(assetID);
		if (!meta || meta->type != AssetType::Texture) { return ImTextureID{}; }

		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			*graphicsCore, assetDatabase, assetID,
			TextureColorSpace::SRGB);
		if (texture && texture->valid) {
			return static_cast<ImTextureID>(texture->gpuHandle.ptr);
		}
		return ImTextureID{};
	}

	AssetType GuessDroppedAssetType(const EditorAssetDragDropPayload& payload) {
		const std::string assetPath = Engine::Algorithm::ToLower(payload.assetPath);
		const std::filesystem::path path = Engine::Algorithm::PathFromUTF8(assetPath);
		const std::string extension = Engine::Algorithm::ToLower(
			Engine::Algorithm::PathToUTF8(path.extension()));
		if (Engine::Algorithm::EndsWith(assetPath, ".animclip.json") || extension == ".animclip") {
			return AssetType::AnimationClip;
		}
		return AssetType::Unknown;
	}

	bool IsAcceptedAssetType(AssetType type, const std::initializer_list<AssetType>& acceptedTypes) {
		if (acceptedTypes.size() == 0) { return true; } // 空リストなら全許可
		return std::find(acceptedTypes.begin(), acceptedTypes.end(), type) != acceptedTypes.end();
	}

	bool TryReadAssetPayload(const ImGuiPayload* payload, EditorAssetDragDropPayload& outPayload) {
		if (!payload || payload->DataSize != sizeof(EditorAssetDragDropPayload)) { return false; }
		outPayload = *static_cast<const EditorAssetDragDropPayload*>(payload->Data);
		return true;
	}

	bool TryReadEntityPayload(const ImGuiPayload* payload, UUID& outUUID) {
		if (!payload || payload->DataSize != sizeof(UUID)) { return false; }
		outUUID = *static_cast<const UUID*>(payload->Data);
		return true;
	}

	//============================================================================
	//	Drawing Helpers
	//============================================================================

	void DrawTextFields(const char* label, const std::array<char, 4>& axes, const float* values, uint32_t count, uint32_t precision) {
		if (!MyGUI::BeginPropertyRow(label)) { return; }
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float totalWidth = ImGui::GetContentRegionAvail().x;
		const float itemWidth = (totalWidth - (spacing * (count - 1))) / count;

		for (uint32_t i = 0; i < count; ++i) {
			if (i > 0) { ImGui::SameLine(0, spacing); }
			ImGui::BeginGroup();
			AxisDisplayInfo axis = GetAxisDisplayInfo(axes[i]);
			ImGui::TextColored(axis.color, "%s", axis.name);
			ImGui::SameLine(0, spacing);
			ImGui::SetNextItemWidth(itemWidth - kAxisLabelWidth - spacing);
			ImGui::TextUnformatted(FormatFloat(values[i], precision).c_str());
			ImGui::EndGroup();
		}
		MyGUI::EndPropertyRow();
	}

	ValueEditResult DrawDragFields(const char* label, const std::array<char, 4>& axes, float* values, uint32_t count, const FloatEditSetting& setting) {
		ValueEditResult result{};
		// プロパティ行を開始しラベルを表示
		if (!MyGUI::BeginPropertyRow(label, setting.propertyRow)) { return result; }

		// ラベル部分のドラッグによる一括変更
		{
			ImGui::TableSetColumnIndex(0);
			// 直前に描画されたラベルの領域を取得
			const ImVec2 labelMin = ImGui::GetItemRectMin();
			const ImVec2 labelMax = ImGui::GetItemRectMax();
			const ImVec2 labelSize = ImVec2(labelMax.x - labelMin.x, labelMax.y - labelMin.y);

			// ラベル部分に見えないボタンを配置してドラッグを検出
			ImGui::SetCursorScreenPos(labelMin);
			ImGui::InvisibleButton("##LabelDrag", labelSize);

			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				float dragSpeed = setting.dragSpeed;
				if (ImGui::GetIO().KeyCtrl) { dragSpeed *= 0.1f; }
				if (ImGui::GetIO().KeyShift) { dragSpeed *= 10.0f; }

				const float delta = ImGui::GetIO().MouseDelta.x * dragSpeed;
				if (delta != 0.0f) {
					for (uint32_t i = 0; i < count; ++i) {
						values[i] += delta;
						if (setting.minValue < setting.maxValue) {
							values[i] = std::clamp(values[i], setting.minValue, setting.maxValue);
						}
					}
					result.valueChanged = true;
				}
			}
			result.anyItemActive |= ImGui::IsItemActive();
			if (ImGui::IsItemDeactivated()) {
				result.editFinished = true;
			}

			ImGui::TableSetColumnIndex(1);
		}

		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		// 右側に別UI(リセットボタン等)を置くための余白を差し引いてフィールド幅を決める
		const float totalWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - setting.reserveRightWidth);
		const float itemWidth = (totalWidth - (spacing * (count - 1))) / count;

		for (uint32_t i = 0; i < count; ++i) {
			if (i > 0) { ImGui::SameLine(0, spacing); }
			ImGui::PushID(i);
			ImGui::BeginGroup();
			// 軸ラベルのX/Y/Z等を色付きで表示
			AxisDisplayInfo axis = GetAxisDisplayInfo(axes[i]);
			ImGui::TextColored(axis.color, "%s", axis.name);
			ImGui::SameLine(0, spacing);
			ImGui::SetNextItemWidth(itemWidth - kAxisLabelWidth - spacing);

			// ドラッグ操作による数値入力
			if (ImGui::DragFloat("##Value", &values[i], setting.dragSpeed, setting.minValue, setting.maxValue, "%.3f", setting.flags)) {
				result.valueChanged = true;
			}
			result.anyItemActive |= ImGui::IsItemActive();
			result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();

			ImGui::EndGroup();
			ImGui::PopID();
		}
		// closeOnProperty=falseのときは行を閉じない
		if (setting.closeOnProperty) {
			MyGUI::EndPropertyRow();
		}
		return result;
	}
} // Engine
