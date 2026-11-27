#include "MaterialPaintTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

#include <Engine/Editor/UI/Panels/Builtin/ProjectAssetPreview.h>
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

#include <algorithm>

namespace Engine {

	void MaterialPaintTool::SavePalette() {

		nlohmann::json data = {{"materials", nlohmann::json::array()}, {"selected", ToAssetReferenceJson(activeMaterial_)}};
		for (AssetID asset : palette_) data["materials"].push_back(ToAssetReferenceJson(asset));
		if (!JsonFile::SaveCanonical(palettePath_, data, 2)) diagnostic_ = "マテリアルリストを保存できません";
	}

	void MaterialPaintTool::DrawPalette(const EditorPanelContext& context, AssetDatabase& database) {

		const auto path = RuntimePaths::GetUserSettingsPath("MaterialPaintPalette.json");
		if (palettePath_ != path) {
			palettePath_ = path;
			palette_.clear();
			activeMaterial_ = {};
			layerMaterial_ = {};
			nlohmann::json data;
			if (JsonFile::TryLoad(path, data) && data.is_object() && data.contains("materials") && data["materials"].is_array()) {
				for (const auto& value : data["materials"]) {
					if (!value.is_string()) continue;
					const AssetID id = FromString32Hex(value.get_ref<const std::string&>());
					if (id && std::find(palette_.begin(), palette_.end(), id) == palette_.end()) palette_.push_back(id);
				}
				const AssetID saved = ParseAssetID(data, "selected");
				if (std::find(palette_.begin(), palette_.end(), saved) != palette_.end()) activeMaterial_ = saved;
				else if (!palette_.empty()) activeMaterial_ = palette_.front();
				layerMaterial_ = activeMaterial_;
			}
		}

		ImGui::SeparatorText("素材");
		AssetID dropped{};
		if (MyGUI::AssetReferenceField("マテリアルリストに追加", dropped, &database, {AssetType::Material}, {}).valueChanged && dropped) {
			const auto* material = context.renderPipeline ? context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(dropped) : nullptr;
			diagnostic_ = material ? MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(*material) : "マテリアルを読み込めません";
			if (diagnostic_.empty()) {
				if (std::find(palette_.begin(), palette_.end(), dropped) == palette_.end()) palette_.push_back(dropped);
				activeMaterial_ = layerMaterial_ = dropped;
				SavePalette();
			}
		}

		const float size = ImGui::GetFrameHeight() * 3.0f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (size + spacing)));
		const auto background = EditorPalette::RGBA("Panels.Model.projectBackground");
		const ImU32 fill = ImGui::GetColorU32(ImVec4(background.r, background.g, background.b, background.a));
		for (size_t i = 0; i < palette_.size(); ++i) {
			if (i % columns != 0) ImGui::SameLine();
			const AssetID asset = palette_[i];
			const auto* meta = database.Find(asset);
			const std::string key = ToString(asset);
			const std::string name = meta ? Algorithm::PathToUTF8(Algorithm::PathFromUTF8(meta->assetPath).stem()) : "素材が見つかりません";
			ImGui::PushID(key.c_str());
			const bool visible = ImGui::IsRectVisible({size, size});
			if (ImGui::InvisibleButton("##Material", {size, size}) && meta) {
				activeMaterial_ = layerMaterial_ = asset;
				SavePalette();
			}
			if (visible) {
				const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
				auto* draw = ImGui::GetWindowDrawList();
				draw->AddRectFilled(min, max, fill);
				ImTextureID texture{};
				ImVec2 uv0{}, uv1{};
				if (context.assetPreviews && meta) {
					context.assetPreviews->Request(asset);
					if (context.assetPreviews->TryGetImage(asset, texture, uv0, uv1)) draw->AddImage(texture, min, max, uv0, uv1);
				}
				if (!texture) {
					const char* text = meta ? "…" : "?";
					const ImVec2 extent = ImGui::CalcTextSize(text);
					draw->AddText({min.x + (size - extent.x) * 0.5f, min.y + (size - extent.y) * 0.5f},
						ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
				}
				const auto border = asset == activeMaterial_ ? ImGuiCol_CheckMark : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Border;
				draw->AddRect(min, max, ImGui::GetColorU32(border), 0.0f, 0, asset == activeMaterial_ ? 2.0f : 1.0f);
				ImGui::SetItemTooltip("%s", name.c_str());
			}
			bool remove = false;
			if (ImGui::BeginPopupContextItem()) {
				remove = ImGui::MenuItem("リストから削除");
				ImGui::EndPopup();
			}
			ImGui::PopID();
			if (remove) {
				palette_.erase(palette_.begin() + i);
				if (activeMaterial_ == asset) activeMaterial_ = palette_.empty() ? AssetID{} : palette_[(std::min)(i, palette_.size() - 1)];
				SavePalette();
				break;
			}
		}
	}

	void MaterialPaintTool::DrawLayers(const EditorPanelContext& context) {

		if (!context.editorContext || !context.editorContext->assetDatabase) return;
		auto* db = context.editorContext->assetDatabase;
		DrawPalette(context, *db);
		if (!context.editorState || !context.GetWorld()) return;
		auto& state = *context.editorState;
		auto& world = *context.GetWorld();
		const Entity primary = state.selectedEntity;
		const auto& objects = state.GetSelectedEntities();
		if (!world.IsAlive(primary)) {
			MyGUI::Text("対象", "ペイントするオブジェクトを選択してください");
			return;
		}
		const auto* sceneObject = world.TryGetComponent<NameComponent>(primary);
		MyGUI::Text("対象", objects.size() > 1 ? std::to_string(objects.size()) + " 個のオブジェクト" :
			(sceneObject ? sceneObject->name : std::string("オブジェクト")));
		ImGui::SeparatorText("素材レイヤー");
		const auto* paint = world.TryGetComponent<MaterialPaintComponent>(primary);
		if (!paint) {
			int res = resolution_ == 512 ? 0 : resolution_ == 1024 ? 1 : 2;
			if (ImGui::Combo("マスク解像度", &res, "512\0" "1024\0" "2048\0")) resolution_ = 512u << res;
			return;
		}
		MyGUI::TextInt("マスク解像度", static_cast<int>(paint->resolution));
		auto edited = *paint;
		bool changed = false;
		for (size_t i = 0; i < edited.layers.size(); ++i) {
			auto& layer = edited.layers[i];
			const auto* meta = db->Find(layer.material);
			ImGui::PushID(static_cast<int>(i));
			changed |= ImGui::Checkbox("##Visible", &layer.visible);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("表示");
			ImGui::SameLine();
			const std::string name = meta ? Algorithm::PathToUTF8(Algorithm::PathFromUTF8(meta->assetPath).stem()) : "素材が見つかりません";
			if (ImGui::Selectable(name.c_str(), layerMaterial_ == layer.material)) {
				layerMaterial_ = layer.material;
				const auto* selected = context.renderPipeline ? context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(layer.material) : nullptr;
				diagnostic_ = selected ? MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(*selected) : "マテリアルを読み込めません";
				if (diagnostic_.empty()) {
					activeMaterial_ = layer.material;
					if (std::find(palette_.begin(), palette_.end(), activeMaterial_) != palette_.end()) SavePalette();
				}
			}
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("手前へ", nullptr, false, i + 1 < edited.layers.size())) {
					std::swap(edited.layers[i], edited.layers[i + 1]); changed = true;
				}
				if (ImGui::MenuItem("奥へ", nullptr, false, i != 0)) {
					std::swap(edited.layers[i], edited.layers[i - 1]); changed = true;
				}
				if (ImGui::MenuItem("削除")) {
					edited.layers.erase(edited.layers.begin() + i); changed = true;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			if (changed) break;
		}
		const auto active = std::find_if(edited.layers.begin(), edited.layers.end(),
			[&](const MaterialPaintLayer& layer) { return layer.material == layerMaterial_; });
		if (active != edited.layers.end()) {
			changed |= ImGui::Checkbox("高さブレンド", &active->heightBlend);
			if (active->heightBlend && ImGui::InputFloat("高さのコントラスト", &active->heightContrast,
				0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
				active->heightContrast = std::clamp(active->heightContrast, 0.0f, 16.0f);
				changed = true;
			}
		}
		const auto count = std::max<size_t>(1, GetMeshSubMeshes(static_cast<const ECSWorld&>(world), primary).size());
		int baseSlot = static_cast<int>(std::min<size_t>(count - 1, state.selectedSubMeshIndex));
		if (count > 1) {
			auto& slot = baseSlot_;
			slot = std::clamp(slot, 0, int(count - 1));
			ImGui::SliderInt("下地のサブメッシュ", &slot, 0, int(count - 1));
			baseSlot = slot;
		}
		AssetID height = size_t(baseSlot) < edited.baseHeightTextures.size() ? edited.baseHeightTextures[baseSlot] : AssetID{};
		if (MyGUI::AssetReferenceField("下地の高さ", height, db, {AssetType::Texture}, {}).valueChanged) {
			edited.baseHeightTextures.resize(count);
			edited.baseHeightTextures[baseSlot] = height;
			changed = true;
		}
		if (changed && context.host) {
			std::vector<MaterialPaintEdit> edits{{world.GetUUID(primary), *paint, std::move(edited)}};
			context.host->ExecuteEditorCommand(std::make_unique<MaterialPaintCommand>(std::move(edits)));
		}
	}
}
