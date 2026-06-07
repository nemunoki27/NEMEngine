#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

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

namespace Engine {

	std::string FormatFloat(float value, uint32_t precision) {
		return std::format("{:.{}f}", value, precision);
	}

	std::string MakeAssetDisplayNameFromPath(const std::string& assetPath) {
		const std::filesystem::path path(assetPath);
		const std::string fileName = path.filename().string();
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		static constexpr const char* suffixes[] = { ".scene.json", ".prefab.json", ".material.json", ".shader.json", ".pipeline.json", ".animclip.json", ".graph.json" };
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

		auto& texService = graphicsCore->GetTextureUploadService();
		const std::string previewKey = "gui:texture:preview:" + meta->assetPath;
		// まだロード要求が出ていないなら開始
		if (texService.GetState(previewKey) == TextureRequestState::None) {
			TextureFileRequestDesc desc{};
			desc.key = previewKey;
			desc.assetPath = meta->assetPath;
			desc.forceSRGB = true;
			texService.RequestTextureFile(desc);
		}
		// ロード完了済みならGPUハンドルを返す
		if (const GPUTextureResource* tex = texService.GetTexture(previewKey)) {
			if (tex->valid) { return static_cast<ImTextureID>(tex->gpuHandle.ptr); }
		}
		return ImTextureID{};
	}

	AssetType GuessDroppedAssetType(const EditorAssetDragDropPayload& payload) {
		const std::string assetPath = Engine::Algorithm::ToLower(payload.assetPath);
		const std::filesystem::path path(assetPath);
		const std::string extension = Engine::Algorithm::ToLower(path.extension().string());
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
		// プロパティ行を開始。ラベルを表示
		if (!MyGUI::BeginPropertyRow(label, setting.propertyRow)) { return result; }

		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		// 右側に別UI(リセットボタン等)を置くための余白を差し引いてフィールド幅を決める
		const float totalWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - setting.reserveRightWidth);
		const float itemWidth = (totalWidth - (spacing * (count - 1))) / count;

		for (uint32_t i = 0; i < count; ++i) {
			if (i > 0) { ImGui::SameLine(0, spacing); }
			ImGui::PushID(i);
			ImGui::BeginGroup();
			// 軸ラベル（X/Y/Z等）を色付きで表示
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