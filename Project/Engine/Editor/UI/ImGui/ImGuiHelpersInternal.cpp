#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
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

//============================================================================
//	Easing functions
//============================================================================
bool Easing::DrawSelector(const char* label, EasingType& easingType, float itemWidth,
	std::span<const Engine::ImGuiUtility::EnumOptionState> options) {

	const auto before = easingType;
	const auto enabled = [&](EasingType value) {
		const auto index = Engine::EnumAdapter<EasingType>::GetIndex(value);
		return options.size() <= index || options[index].enabled;
	};

	ImGui::SetNextItemWidth(itemWidth);
	if (!ImGui::BeginCombo(label, Engine::EnumAdapter<EasingType>::ToString(easingType))) return false;

	if (enabled(EasingType::Linear) && ImGui::Button("Linear", ImVec2(itemWidth, 24.0f))) {
		easingType = EasingType::Linear;
	}

	ImGui::PushItemWidth(itemWidth);
	const auto group = [&](const char* name, EasingType first, EasingType last) {
		if (!ImGui::BeginCombo(name, "")) return;
		for (int i = static_cast<int>(first); i <= static_cast<int>(last); ++i) {
			const auto value = static_cast<EasingType>(i);
			if (!enabled(value)) continue;
			const bool selected = easingType == value;
			if (ImGui::Selectable(Engine::EnumAdapter<EasingType>::ToString(value), selected)) easingType = value;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	};
	group("EaseIn", EasingType::EaseInSine, EasingType::EaseInBounce);
	group("EaseOut", EasingType::EaseOutSine, EasingType::EaseOutBounce);
	group("EaseInOut", EasingType::EaseInOutSine, EasingType::EaseInOutBounce);
	ImGui::PopItemWidth();
	ImGui::EndCombo();
	return before != easingType;
}

void Easing::SelectEasingType(EasingType& easingType, const std::string& label, float itemWidth) {

	DrawSelector(label.c_str(), easingType, itemWidth);
}

namespace Engine {

	AxisDisplayInfo GetAxisDisplayInfo(char axis, AxisLabelPalette palette) {

		// コピー表示と編集表示の既存色を保持する
		const bool copyable = palette == AxisLabelPalette::Copyable;
		switch (axis) {
		case 'R':
			return {"R", Engine::EditorPalette::Get("Panels.Field.red")};
		case 'G':
			return {"G", Engine::EditorPalette::Get("Panels.Field.green")};
		case 'B':
			return {"B", Engine::EditorPalette::Get("Panels.Field.blue")};
		case 'A':
			return {"A", Engine::EditorPalette::Get("Panels.Field.alpha")};
		case 'X':
			return {"X", copyable ? Engine::EditorPalette::Get("Panels.Field.copyX") : Engine::EditorPalette::Get("Panels.Field.red")};
		case 'Y':
			return {"Y", copyable ? Engine::EditorPalette::Get("Panels.Field.copyY") : Engine::EditorPalette::Get("Panels.Field.blue")};
		case 'Z':
			return {"Z", copyable ? Engine::EditorPalette::Get("Panels.Field.copyZ") : Engine::EditorPalette::Get("Panels.Field.green")};
		case 'W':
			return {"W", Engine::EditorPalette::Get("Panels.Field.alpha")};
		default:
			return {"", Engine::EditorPalette::Get("Panels.Field.other")};
		}
	}

	std::string FormatFloat(float value, uint32_t precision) {
		return std::format("{:.{}f}", value, precision);
	}

	std::string MakeAssetDisplayNameFromPath(const std::string& assetPath) {
		const std::filesystem::path path = Engine::Algorithm::PathFromUTF8(assetPath);
		const std::string fileName = Engine::Algorithm::PathToUTF8(path.filename());
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		static constexpr const char* suffixes[] = {
			".scene.json", ".prefab.json", ".material.json", ".shader.json", ".pipeline.json", ".animclip.json", ".graph.json"};
		for (const char* suffix : suffixes) {
			if (Engine::Algorithm::EndsWith(lower, suffix)) {
				return fileName.substr(0, fileName.size() - std::strlen(suffix));
			}
		}
		return fileName;
	}

	ImTextureID ResolveTextureAssetPreview(GraphicsCore* graphicsCore, const AssetDatabase* assetDatabase, AssetID assetID) {
		if (!graphicsCore || !assetDatabase || !assetID) {
			return ImTextureID{};
		}
		const AssetMeta* meta = assetDatabase->Find(assetID);
		if (!meta || (meta->type != AssetType::Texture && meta->type != AssetType::RenderTexture)) {
			return ImTextureID{};
		}

		const GPUTextureResource* texture =
			RuntimeTextureResolver::Resolve(*graphicsCore, assetDatabase, assetID, TextureColorSpace::SRGB);
		if (texture && texture->valid) {
			return static_cast<ImTextureID>(texture->gpuHandle.ptr);
		}
		return ImTextureID{};
	}

	AssetType GuessDroppedAssetType(const EditorAssetDragDropPayload& payload) {
		const std::string assetPath = Engine::Algorithm::ToLower(payload.assetPath);
		const std::filesystem::path path = Engine::Algorithm::PathFromUTF8(assetPath);
		const std::string extension = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(path.extension()));
		if (Engine::Algorithm::EndsWith(assetPath, ".prefab.json") || extension == ".prefab") {
			return AssetType::Prefab;
		}
		if (Engine::Algorithm::EndsWith(assetPath, ".animclip.json") || extension == ".animclip") {
			return AssetType::AnimationClip;
		}
		if (Engine::Algorithm::EndsWith(assetPath, ".timeline.json") || extension == ".timeline") {
			return AssetType::Timeline;
		}
		return AssetType::Unknown;
	}

	AssetType ResolveDroppedAssetType(const EditorAssetDragDropPayload& payload, const AssetDatabase* assetDatabase) {

		if (payload.assetType != AssetType::Unknown) {
			return payload.assetType;
		}
		if (assetDatabase) {
			if (const AssetMeta* meta = assetDatabase->Find(payload.assetID); meta && meta->type != AssetType::Unknown) {
				return meta->type;
			}
		}
		return GuessDroppedAssetType(payload);
	}

	bool IsAcceptedAssetType(AssetType type, const std::initializer_list<AssetType>& acceptedTypes) {
		if (acceptedTypes.size() == 0) {
			return true;
		} // 空リストなら全許可
		if (type == AssetType::RenderTexture && std::find(acceptedTypes.begin(), acceptedTypes.end(), AssetType::Texture) != acceptedTypes.end())
			return true;
		return std::find(acceptedTypes.begin(), acceptedTypes.end(), type) != acceptedTypes.end();
	}

	bool TryReadAssetPayload(const ImGuiPayload* payload, EditorAssetDragDropPayload& outPayload) {
		if (!payload || payload->DataSize != sizeof(EditorAssetDragDropPayload)) {
			return false;
		}
		outPayload = *static_cast<const EditorAssetDragDropPayload*>(payload->Data);
		return true;
	}

	bool TryReadEntityPayload(const ImGuiPayload* payload, UUID& outUUID) {
		if (!payload || payload->DataSize != sizeof(UUID)) {
			return false;
		}
		outUUID = *static_cast<const UUID*>(payload->Data);
		return true;
	}

	//============================================================================
	//	Drawing Helpers
	//============================================================================

	ValueEditResult DrawDragFields(
		const char* label, const std::array<char, 4>& axes, float* values, uint32_t count, const FloatEditSetting& setting) {
		ValueEditResult result{};
		// プロパティ行を開始しラベルを表示
		if (!MyGUI::BeginPropertyRow(label, setting.propertyRow)) {
			return result;
		}

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
				if (ImGui::GetIO().KeyCtrl) {
					dragSpeed *= 0.1f;
				}
				if (ImGui::GetIO().KeyShift) {
					dragSpeed *= 10.0f;
				}

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
			if (i > 0) {
				ImGui::SameLine(0, spacing);
			}
			ImGui::PushID(i);
			ImGui::BeginGroup();
			// 軸ラベルのX/Y/Z等を色付きで表示
			const AxisDisplayInfo axis = GetAxisDisplayInfo(axes[i]);
			const bool hasAxis = axis.name[0] != '\0';
			if (hasAxis) {
				ImGui::TextColored(axis.color, "%s", axis.name);
				ImGui::SameLine(0, spacing);
			}
			// 軸がない項目は入力領域を全幅で使う
			const float axisWidth = hasAxis ? ImGui::CalcTextSize(axis.name).x + spacing : 0.0f;
			ImGui::SetNextItemWidth((std::max)(1.0f, itemWidth - axisWidth));

			// ドラッグ操作による数値入力
			if (ImGui::DragFloat(
					"##Value", &values[i], setting.dragSpeed, setting.minValue, setting.maxValue, "%.3f", setting.flags)) {
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
