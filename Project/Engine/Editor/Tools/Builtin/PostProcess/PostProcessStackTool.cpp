#include "PostProcessStackTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/IDentity/UUID.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// imgui
#include <imgui.h>
#include <imgui_stdlib.h>

// c++
#include <algorithm>
#include <filesystem>
#include <string>
#include <type_traits>

//============================================================================
//	PostProcessStackTool classMethods
//============================================================================

namespace {

	constexpr const char* kPassReorderPayloadType = "PP_PASS_REORDER";
	constexpr const char* kUnsavedPopupId = "UnsavedPostProcessStack##Popup";

	bool EndsWith(const std::string_view& str, const std::string_view& suffix) {

		if (str.size() < suffix.size()) {
			return false;
		}
		return str.rfind(suffix) == str.size() - suffix.size();
	}

	// 変数タイプからMaterialParameterValueを生成する
	Engine::MaterialParameterValue DefaultValueForVariable(const Engine::ShaderConstantBufferVariable& var) {

		Engine::MaterialParameterValue result{};
		if (var.valueType == D3D_SVT_FLOAT) {
			if (var.columns <= 1) {
				result.value = 0.0f;
			} else if (var.columns == 2) {
				result.value = Engine::Vector2{};
			} else if (var.columns == 3) {
				result.value = Engine::Vector3{};
			} else {
				result.value = Engine::Vector4{};
			}
		} else if (var.valueType == D3D_SVT_INT) {
			result.value = int32_t(0);
		} else if (var.valueType == D3D_SVT_UINT) {
			result.value = uint32_t(0);
		} else if (var.valueType == D3D_SVT_BOOL) {
			result.value = false;
		} else {
			result.value = 0.0f;
		}
		return result;
	}

	// MaterialParameterValueを変数タイプに応じてUIで編集する
	bool DrawParameterValueEdit(const char* label, Engine::MaterialParameterValue& value) {

		return std::visit([&](auto& v) -> bool {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, float>) {
				return Engine::MyGUI::DragFloat(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, Engine::Vector2>) {
				return Engine::MyGUI::DragVector2(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, Engine::Vector3>) {
				return Engine::MyGUI::DragVector3(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, Engine::Vector4>) {
				return Engine::MyGUI::DragVector4(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, Engine::Color4>) {
				return Engine::MyGUI::ColorEdit(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, int32_t>) {
				return Engine::MyGUI::DragInt(label, v).valueChanged;
			} else if constexpr (std::is_same_v<T, uint32_t>) {
				int32_t iv = static_cast<int32_t>(v);
				if (Engine::MyGUI::DragInt(label, iv).valueChanged) {
					v = static_cast<uint32_t>((std::max)(0, iv));
					return true;
				}
				return false;
			} else if constexpr (std::is_same_v<T, bool>) {
				return Engine::MyGUI::Checkbox(label, v);
			} else {
				return false;
			}
			}, value.value);
	}

	bool IsPostProcessStackFile(const std::string& path) {

		return EndsWith(path, ".postProcessStack.json");
	}

	bool IsMaterialJsonFile(const std::string& path) {

		return EndsWith(path, ".material.json");
	}
}

void Engine::PostProcessStackTool::Tick(ToolContext& context) {

	if (!context.activeSceneHeader) {
		return;
	}

	const std::string& nextPath = context.activeSceneHeader->postProcessStackPath;
	if (nextPath == lastScenePath_) {
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	if (service.IsDirty()) {

		// 変更がある場合は確認ポップアップを予約する
		pendingScenePathChange_ = true;
		pendingNextScenePath_ = nextPath;
	} else {

		service.SetActiveSettingsAssetPath(nextPath);
		lastScenePath_ = nextPath;
		selectedPassIndex_ = -1;
	}
}

void Engine::PostProcessStackTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::PostProcessStackTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::PostProcessStackTool::DrawWindow(const EditorToolContext& context) {

	ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("PostProcessStack", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::SetWindowFontScale(0.64f);

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();

	// 現在の設定ファイルパスと未保存マーカーを表示する
	{
		const std::string pathStr = service.GetCurrentPath().generic_string();
		if (service.IsDirty()) {
			ImGui::TextDisabled("Settings: %s *", pathStr.c_str());
		} else {
			ImGui::TextDisabled("Settings: %s", pathStr.c_str());
		}
	}

	// Save / Reload ボタン
	if (ImGui::Button("Save")) {
		service.Save();
		service.ClearDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		service.Reload();
		selectedPassIndex_ = -1;
	}

	ImGui::Separator();

	// .postProcessStack.json ドロップゾーン
	DrawDropZones(context);

	ImGui::Separator();

	// 左右カラム分割: 左=パス一覧、右=詳細
	const float totalWidth = ImGui::GetContentRegionAvail().x;
	const float leftWidth = totalWidth * 0.35f;
	const float rightWidth = totalWidth - leftWidth - ImGui::GetStyle().ItemSpacing.x;

	ImGui::BeginGroup();
	ImGui::SetNextWindowContentSize(ImVec2(leftWidth, 0.0f));
	if (ImGui::BeginChild("##PassList", ImVec2(leftWidth, 0.0f), true)) {
		DrawPassList();
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	if (ImGui::BeginChild("##PassDetail", ImVec2(rightWidth, 0.0f), true)) {
		DrawPassDetail(context);
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	// 未保存確認ポップアップ
	DrawUnsavedConfirmPopup();

	ImGui::SetWindowFontScale(0.64f);
	ImGui::End();
}

void Engine::PostProcessStackTool::DrawPassList() {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();
	auto& passes = settings.passes;

	ImGui::TextUnformatted("Passes");
	ImGui::Separator();

	int32_t reorderFrom = -1;
	int32_t reorderTo = -1;

	for (int32_t i = 0; i < static_cast<int32_t>(passes.size()); ++i) {

		auto& pass = passes[i];
		ImGui::PushID(i);

		// 有効チェックボックス
		if (ImGui::Checkbox("##en", &pass.enabled)) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
		ImGui::SameLine();

		// パス名の選択アイテム
		const bool isSelected = (selectedPassIndex_ == i);
		if (ImGui::Selectable(pass.name.empty() ? "(Unnamed)" : pass.name.c_str(),
			isSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
			selectedPassIndex_ = i;
		}

		// ドラッグソース: パスの並び替え
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload(kPassReorderPayloadType, &i, sizeof(int32_t));
			ImGui::TextUnformatted(pass.name.empty() ? "(Unnamed)" : pass.name.c_str());
			ImGui::EndDragDropSource();
		}

		// ドラッグターゲット: ここへドロップで並び替える
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPassReorderPayloadType)) {
				reorderFrom = *static_cast<const int32_t*>(payload->Data);
				reorderTo = i;
			}
			ImGui::EndDragDropTarget();
		}

		// 右クリックコンテキストメニュー
		if (ImGui::BeginPopupContextItem("##PassContext")) {
			if (ImGui::MenuItem("Delete")) {
				passes.erase(passes.begin() + i);
				if (selectedPassIndex_ >= static_cast<int32_t>(passes.size())) {
					selectedPassIndex_ = static_cast<int32_t>(passes.size()) - 1;
				}
				service.MarkDirty();
				service.RebuildRuntime();
				ImGui::EndPopup();
				ImGui::PopID();
				break;
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	// 並び替え処理
	if (reorderFrom >= 0 && reorderTo >= 0 && reorderFrom != reorderTo) {

		const int32_t passCount = static_cast<int32_t>(passes.size());
		if (reorderFrom < passCount && reorderTo < passCount) {

			PostProcessStackPassSettings moved = std::move(passes[reorderFrom]);
			passes.erase(passes.begin() + reorderFrom);
			const int32_t insertAt = (reorderTo > reorderFrom) ? reorderTo - 1 : reorderTo;
			passes.insert(passes.begin() + insertAt, std::move(moved));

			if (selectedPassIndex_ == reorderFrom) {
				selectedPassIndex_ = insertAt;
			}
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}
}

void Engine::PostProcessStackTool::DrawPassDetail(const EditorToolContext& context) {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();
	auto& passes = settings.passes;

	if (selectedPassIndex_ < 0 || selectedPassIndex_ >= static_cast<int32_t>(passes.size())) {
		ImGui::TextDisabled("Select a pass to edit.");
		return;
	}

	PostProcessStackPassSettings& pass = passes[selectedPassIndex_];

	// パス名編集
	if (MyGUI::InputText("Name", pass.name).editFinished) {
		service.MarkDirty();
	}

	// パスシェーダーパス名
	if (MyGUI::InputText("Pass Name", pass.passName).editFinished) {
		service.MarkDirty();
		service.RebuildRuntime();
	}

	ImGui::Separator();

	// マテリアル参照フィールド
	{
		AssetID matGuid = pass.materialGuid;
		AssetEditSetting setting{};
		if (MyGUI::AssetReferenceField("Material", matGuid,
			context.toolContext.assetDatabase, { AssetType::Material }, setting).valueChanged) {

			pass.materialGuid = matGuid;
			if (context.toolContext.assetDatabase && matGuid) {
				const AssetMeta* meta = context.toolContext.assetDatabase->Find(matGuid);
				pass.materialPathCache = meta ? meta->assetPath : "";
			} else {
				pass.materialPathCache.clear();
			}
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	ImGui::Separator();

	// CBuffer パラメータオーバーライドUI (Reflectionキャッシュ使用)
	const std::vector<ShaderConstantBufferVariable>* vars = service.FindReflectionVars(pass.materialGuid);
	if (vars && !vars->empty()) {

		ImGui::TextUnformatted("Parameters");
		ImGui::Separator();

		bool anyParamChanged = false;
		for (const auto& var : *vars) {

			ImGui::PushID(var.name.c_str());

			auto it = pass.parameterOverrides.find(var.name);
			if (it == pass.parameterOverrides.end()) {

				// オーバーライドなし: デフォルト値を灰色で表示し、+ボタンでオーバーライドを追加する
				ImGui::TextDisabled("[default] %s", var.name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("+##Override")) {
					pass.parameterOverrides[var.name] = DefaultValueForVariable(var);
					anyParamChanged = true;
				}
			} else {

				// オーバーライドあり: 値を編集可能に表示し、xボタンで削除できる
				if (DrawParameterValueEdit(var.name.c_str(), it->second)) {
					anyParamChanged = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x##RemoveOverride")) {
					pass.parameterOverrides.erase(it);
					anyParamChanged = true;
				}
			}

			ImGui::PopID();
		}

		if (anyParamChanged) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	// テクスチャSRVオーバーライドUI (Reflectionキャッシュ使用)
	const std::vector<ShaderResourceBinding>* srvs = service.FindReflectionSRVs(pass.materialGuid);
	if (srvs && !srvs->empty()) {

		ImGui::TextUnformatted("Textures");
		ImGui::Separator();

		bool anySRVChanged = false;
		for (const auto& srv : *srvs) {

			ImGui::PushID(srv.name.c_str());

			auto it = pass.textureGuids.find(srv.name);
			AssetID texGuid = (it != pass.textureGuids.end()) ? it->second : AssetID{};

			AssetEditSetting setting{};
			if (MyGUI::AssetReferenceField(srv.name.c_str(), texGuid,
				context.toolContext.assetDatabase, { AssetType::Texture }, setting).valueChanged) {

				if (texGuid) {
					pass.textureGuids[srv.name] = texGuid;
					if (context.toolContext.assetDatabase) {
						const AssetMeta* meta = context.toolContext.assetDatabase->Find(texGuid);
						pass.texturePathCaches[srv.name] = meta ? meta->assetPath : "";
					}
				} else {
					pass.textureGuids.erase(srv.name);
					pass.texturePathCaches.erase(srv.name);
				}
				anySRVChanged = true;
			}

			ImGui::PopID();
		}

		if (anySRVChanged) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}
}

void Engine::PostProcessStackTool::DrawDropZones(const EditorToolContext& context) {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();

	// ドロップゾーン: .postProcessStack.json を読み込む
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button("Drop .postProcessStack.json to load", ImVec2(ImGui::GetContentRegionAvail().x, zoneHeight));
		ImGui::PopStyleColor();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				IEditorPanel::kProjectAssetDragDropPayloadType)) {

				const auto* data = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (data && !data->isDirectory && IsPostProcessStackFile(data->assetPath)) {
					service.SetActiveSettingsAssetPath(data->assetPath);
					lastScenePath_ = data->assetPath;
					selectedPassIndex_ = -1;
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	// ドロップゾーン: マテリアルを追加する
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button("Drop .material.json to add pass", ImVec2(ImGui::GetContentRegionAvail().x, zoneHeight));
		ImGui::PopStyleColor();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				IEditorPanel::kProjectAssetDragDropPayloadType)) {

				const auto* data = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (data && !data->isDirectory) {

					const std::string assetPath = data->assetPath;
					AssetID materialGuid{};
					std::string name = std::filesystem::path(assetPath).stem().string();

					if (IsMaterialJsonFile(assetPath)) {

						materialGuid = data->assetID;
						if (!materialGuid && context.toolContext.assetDatabase) {
							const AssetMeta* meta = context.toolContext.assetDatabase->FindByPath(assetPath);
							if (meta) {
								materialGuid = meta->guid;
							}
						}

						// 拡張子を取り除いた名前を使う
						if (name.size() > 9 && name.substr(name.size() - 9) == ".material") {
							name = name.substr(0, name.size() - 9);
						}
					}

					if (materialGuid) {

						PostProcessStackPassSettings newPass{};
						newPass.id = UUID::New();
						newPass.name = name.empty() ? "NewPass" : name;
						newPass.enabled = true;
						newPass.materialGuid = materialGuid;
						newPass.materialPathCache = assetPath;
						newPass.passName = "PostProcess";

						settings.passes.emplace_back(std::move(newPass));
						selectedPassIndex_ = static_cast<int32_t>(settings.passes.size()) - 1;
						service.MarkDirty();
						service.RebuildRuntime();
					} else if (!IsMaterialJsonFile(assetPath)) {
						Logger::Output(LogType::Engine,
							"[PostProcessStack] Unsupported file dropped: " + assetPath +
							". Drag .material.json to add a pass.");
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
}

void Engine::PostProcessStackTool::DrawUnsavedConfirmPopup() {

	if (pendingScenePathChange_) {
		ImGui::OpenPopup(kUnsavedPopupId);
		pendingScenePathChange_ = false;
	}

	ImGui::SetNextWindowSize(ImVec2(340.0f, 120.0f), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal(kUnsavedPopupId, nullptr,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		return;
	}

	ImGui::TextUnformatted("PostProcessStack has unsaved changes.");
	ImGui::TextUnformatted("Switch scene without saving?");
	ImGui::Spacing();

	PostProcessStackService& service = PostProcessStackService::GetInstance();

	if (ImGui::Button("Save & Switch", ImVec2(110.0f, 0.0f))) {
		service.Save();
		service.ClearDirty();
		service.SetActiveSettingsAssetPath(pendingNextScenePath_);
		lastScenePath_ = pendingNextScenePath_;
		selectedPassIndex_ = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard & Switch", ImVec2(110.0f, 0.0f))) {
		service.SetActiveSettingsAssetPath(pendingNextScenePath_);
		lastScenePath_ = pendingNextScenePath_;
		selectedPassIndex_ = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
		pendingNextScenePath_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}
