#include "RayTracingEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// imgui
#include <imgui.h>
#include <imgui_stdlib.h>

// c++
#include <algorithm>
#include <string>
#include <unordered_set>

namespace {

	constexpr const char* kEffectReorderPayload = "RAY_TRACING_EFFECT_REORDER";
	constexpr const char* kParameterBufferName = "RayTracingParameters";

	Engine::SceneHeader* ResolveActiveSceneHeader(const Engine::ToolContext& context) {

		if (context.sceneInstances && context.activeSceneInstanceID) {
			Engine::SceneInstance* instance = context.sceneInstances->Find(
				context.activeSceneInstanceID);
			if (instance) {
				return &instance->header;
			}
		}
		return const_cast<Engine::SceneHeader*>(context.activeSceneHeader);
	}

	bool IsEditableTextureResource(const Engine::ShaderResourceBinding& binding) {

		return binding.kind == Engine::ShaderBindingKind::SRV &&
			binding.rawType != D3D_SIT_STRUCTURED &&
			binding.rawType != D3D_SIT_BYTEADDRESS &&
			binding.rawType != D3D_SIT_RTACCELERATIONSTRUCTURE;
	}

	Engine::RayTracingInputBinding* FindInput(
		Engine::RayTracingEffectSettings& effect, std::string_view resource) {

		for (Engine::RayTracingInputBinding& input : effect.inputs) {
			if (input.shaderResource == resource) {
				return &input;
			}
		}
		return nullptr;
	}

	Engine::RayTracingTextureBinding* FindTexture(
		Engine::RayTracingEffectSettings& effect, std::string_view resource) {

		for (Engine::RayTracingTextureBinding& texture : effect.textures) {
			if (texture.shaderResource == resource) {
				return &texture;
			}
		}
		return nullptr;
	}

	void RemoveInput(Engine::RayTracingEffectSettings& effect,
		std::string_view resource) {

		std::erase_if(effect.inputs, [resource](const Engine::RayTracingInputBinding& input) {
			return input.shaderResource == resource;
		});
	}

	void RemoveTexture(Engine::RayTracingEffectSettings& effect,
		std::string_view resource) {

		std::erase_if(effect.textures, [resource](const Engine::RayTracingTextureBinding& texture) {
			return texture.shaderResource == resource;
		});
	}
}

//============================================================================
//	RayTracingEditorTool classMethods
//============================================================================
void Engine::RayTracingEditorTool::Tick(ToolContext& context) {

	const AssetID sceneProfile = context.activeSceneHeader ?
		context.activeSceneHeader->rayTracingProfile : AssetID{};
	if (sceneProfile == observedSceneProfile_) {
		return;
	}
	observedSceneProfile_ = sceneProfile;
	if (!dirty_ && sceneProfile) {

		requestedProfile_ = sceneProfile;
	}
}

void Engine::RayTracingEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::RayTracingEditorTool::OpenAsset(AssetID assetID) {

	requestedProfile_ = assetID;
	openWindow_ = true;
}

void Engine::RayTracingEditorTool::DrawEditorTool(
	const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::RayTracingEditorTool::DrawWindow(
	const EditorToolContext& context) {

	ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("レイトレーシング設定", &openWindow_)) {
		ImGui::End();
		return;
	}

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	SceneHeader* sceneHeader = ResolveActiveSceneHeader(context.toolContext);
	if (!assetDatabase || !context.panelContext ||
		!context.panelContext->renderPipeline ||
		!context.panelContext->graphicsCore) {

		ImGui::TextDisabled("描画コンテキストを取得できません");
		ImGui::End();
		return;
	}

	if (requestedProfile_) {
		const AssetID requested = requestedProfile_;
		requestedProfile_ = {};
		LoadProfile(context, requested);
	}

	const GraphicsFeatureController& features =
		context.panelContext->graphicsCore->GetDXObject().GetFeatureController();
	const bool supported = features.GetSupport().SupportsRayTracingPath();
	const bool active = features.ShouldUseDispatchRays();
	ImGui::TextColored(
		active ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f) :
		ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
		"DispatchRays: %s", active ? "有効" :
		(supported ? "グラフィック設定で無効" : "GPU非対応"));

	if (sceneHeader) {
		AssetID sceneProfile = sceneHeader->rayTracingProfile;
		if (MyGUI::AssetReferenceField("シーン設定", sceneProfile,
			assetDatabase, { AssetType::RayTracingProfile }).valueChanged) {

			sceneHeader->rayTracingProfile = sceneProfile;
			observedSceneProfile_ = sceneProfile;
			if (context.panelContext->host) {
				context.panelContext->host->RequestMarkSceneDirty();
			}
			if (sceneProfile) {
				LoadProfile(context, sceneProfile);
			}
		}
	}

	AssetID pickedProfile = selectedProfile_;
	if (MyGUI::AssetReferenceField("編集アセット", pickedProfile,
		assetDatabase, { AssetType::RayTracingProfile }).valueChanged &&
		pickedProfile) {

		LoadProfile(context, pickedProfile);
	}

	ImGui::BeginDisabled(!selectedProfile_ || !dirty_);
	if (ImGui::Button("保存")) {
		SaveProfile(context);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!selectedProfile_);
	if (ImGui::Button("再読み込み")) {
		LoadProfile(context, selectedProfile_);
	}
	ImGui::EndDisabled();

	if (!selectedProfile_) {
		ImGui::Separator();
		ImGui::TextDisabled("Ray Tracing Profileを選択してください");
		ImGui::End();
		return;
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s%s", draft_.name.c_str(), dirty_ ? " *" : "");
	ImGui::Separator();

	const float totalWidth = ImGui::GetContentRegionAvail().x;
	const float leftWidth = (std::max)(220.0f, totalWidth * 0.3f);
	ImGui::BeginGroup();
	if (ImGui::BeginChild("##RayTracingEffectList",
		ImVec2(leftWidth, 0.0f), true)) {

		DrawEffectList();
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	ImGui::SameLine();
	ImGui::BeginGroup();
	if (ImGui::BeginChild("##RayTracingEffectDetail",
		ImVec2(0.0f, 0.0f), true)) {

		DrawEffectDetail(context);
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	if (!statusMessage_.empty()) {
		ImGui::SetCursorPosY((std::max)(0.0f,
			ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 8.0f));
		if (statusError_) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
				"%s", statusMessage_.c_str());
		} else {
			ImGui::TextDisabled("%s", statusMessage_.c_str());
		}
	}
	ImGui::End();
}

void Engine::RayTracingEditorTool::DrawEffectList() {

	if (MyGUI::InputText("名前", draft_.name).valueChanged) {
		SetDirty();
	}
	ImGui::Separator();
	ImGui::TextUnformatted("エフェクト一覧");

	int32_t removeIndex = -1;
	int32_t reorderFrom = -1;
	int32_t reorderTo = -1;
	for (int32_t index = 0;
		index < static_cast<int32_t>(draft_.effects.size()); ++index) {

		RayTracingEffectSettings& effect = draft_.effects[index];
		ImGui::PushID(index);
		if (ImGui::Checkbox("##Enabled", &effect.enabled)) {
			SetDirty();
		}
		ImGui::SameLine();
		if (ImGui::Selectable(effect.name.empty() ? "(名前なし)" : effect.name.c_str(),
			selectedEffectIndex_ == index)) {

			selectedEffectIndex_ = index;
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload(kEffectReorderPayload, &index, sizeof(index));
			ImGui::TextUnformatted(effect.name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload =
				ImGui::AcceptDragDropPayload(kEffectReorderPayload)) {

				reorderFrom = *static_cast<const int32_t*>(payload->Data);
				reorderTo = index;
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("##EffectContext")) {
			if (ImGui::MenuItem("削除")) {
				removeIndex = index;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	if (removeIndex >= 0) {
		draft_.effects.erase(draft_.effects.begin() + removeIndex);
		selectedEffectIndex_ = (std::min)(selectedEffectIndex_,
			static_cast<int32_t>(draft_.effects.size()) - 1);
		SetDirty();
	} else if (reorderFrom >= 0 && reorderTo >= 0 &&
		reorderFrom != reorderTo) {

		RayTracingEffectSettings moved = std::move(draft_.effects[reorderFrom]);
		draft_.effects.erase(draft_.effects.begin() + reorderFrom);
		draft_.effects.insert(draft_.effects.begin() + reorderTo, std::move(moved));
		selectedEffectIndex_ = reorderTo;
		SetDirty();
	}

	ImGui::Spacing();
	if (ImGui::Button("エフェクトを追加",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		RayTracingEffectSettings effect{};
		effect.id = UUID::New();
		effect.name = "Ray Tracing Effect " +
			std::to_string(draft_.effects.size() + 1);
		draft_.effects.emplace_back(std::move(effect));
		selectedEffectIndex_ = static_cast<int32_t>(draft_.effects.size()) - 1;
		SetDirty();
	}
}

void Engine::RayTracingEditorTool::DrawEffectDetail(
	const EditorToolContext& context) {

	if (selectedEffectIndex_ < 0 ||
		selectedEffectIndex_ >= static_cast<int32_t>(draft_.effects.size())) {

		ImGui::TextDisabled("編集するエフェクトを選択してください");
		return;
	}

	RayTracingEffectSettings& effect = draft_.effects[selectedEffectIndex_];
	bool changed = false;
	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("RayTracingEffectSettings");
		changed |= MyGUI::InputText("名前", effect.name).valueChanged;
		changed |= MyGUI::Checkbox("有効", effect.enabled);
		changed |= MyGUI::Checkbox("Game View", effect.gameView);
		changed |= MyGUI::Checkbox("Scene View", effect.sceneView);
		changed |= MyGUI::EnumCombo("実行位置", effect.executionPoint).valueChanged;
		changed |= MyGUI::AssetReferenceField("マテリアル", effect.material,
			context.toolContext.assetDatabase, { AssetType::Material }).valueChanged;
		int32_t rayGenerationIndex = static_cast<int32_t>(effect.rayGenerationIndex);
		if (MyGUI::DragInt("Ray Generation", rayGenerationIndex,
			{ .dragSpeed = 1.0f, .minValue = 0, .maxValue = 255 }).valueChanged) {

			effect.rayGenerationIndex = static_cast<uint32_t>(
				(std::max)(0, rayGenerationIndex));
			changed = true;
		}
	}
	if (changed) {
		SetDirty();
	}

	const ShaderReflectionInfo* reflection = nullptr;
	if (effect.material) {
		reflection = context.panelContext->renderPipeline->
			FindMaterialRayTracingReflection(
				*context.panelContext->graphicsCore, effect.material);
	}
	if (!effect.material) {
		ImGui::TextDisabled("RayTracingマテリアルが未設定です");
	} else if (!reflection) {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
			"RayTracingパスまたは有効なDXRシェーダーを取得できません");
	}

	ImGui::Separator();
	DrawResourceBindings(context, effect, reflection);
	ImGui::Separator();
	DrawParameterOverrides(effect, reflection);
}

void Engine::RayTracingEditorTool::DrawResourceBindings(
	const EditorToolContext& context, RayTracingEffectSettings& effect,
	const ShaderReflectionInfo* reflection) {

	if (!MyGUI::CollapsingHeader("入力リソース", true)) {
		return;
	}
	if (!reflection) {
		ImGui::TextDisabled("シェーダーreflectionを取得すると入力が表示されます");
		return;
	}

	for (const ShaderResourceBinding& resource : reflection->resources) {
		if (!IsEditableTextureResource(resource)) {
			continue;
		}

		ImGui::PushID(resource.name.c_str());
		RayTracingInputBinding* input = FindInput(effect, resource.name);
		RayTracingTextureBinding* texture = FindTexture(effect, resource.name);
		int32_t mode = texture ? 2 : (input ? 1 : 0);
		const char* modes[] = { "未設定", "シーン入力", "テクスチャ" };
		if (ImGui::Combo(resource.name.c_str(), &mode, modes, 3)) {
			RemoveInput(effect, resource.name);
			RemoveTexture(effect, resource.name);
			if (mode == 1) {
				effect.inputs.push_back({ resource.name,
					RayTracingTextureSource::SceneColor });
			} else if (mode == 2) {
				effect.textures.push_back({ resource.name, {} });
			}
			SetDirty();
			input = FindInput(effect, resource.name);
			texture = FindTexture(effect, resource.name);
		}
		if (input && MyGUI::EnumCombo("入力元", input->source).valueChanged) {
			SetDirty();
		}
		if (texture && MyGUI::AssetReferenceField("テクスチャ", texture->texture,
			context.toolContext.assetDatabase,
			{ AssetType::Texture }).valueChanged) {

			SetDirty();
		}
		ImGui::PopID();
	}
}

void Engine::RayTracingEditorTool::DrawParameterOverrides(
	RayTracingEffectSettings& effect,
	const ShaderReflectionInfo* reflection) {

	if (!MyGUI::CollapsingHeader("パラメータ", true)) {
		return;
	}
	if (!reflection) {
		ImGui::TextDisabled("シェーダーreflectionを取得するとパラメータが表示されます");
		return;
	}
	if (MaterialParameterEditor::DrawReflectedCBufferParameters(
		*reflection, kParameterBufferName, effect.parameterOverrides)) {

		SetDirty();
	}
}

bool Engine::RayTracingEditorTool::LoadProfile(
	const EditorToolContext& context, AssetID profileAsset) {

	if (!profileAsset || !context.panelContext ||
		!context.panelContext->renderPipeline) {
		return false;
	}
	RenderAssetLibrary& library =
		context.panelContext->renderPipeline->GetRenderAssetLibrary();
	library.InvalidateRayTracingProfile(profileAsset);
	const RayTracingProfileAsset* profile = library.LoadRayTracingProfile(profileAsset);
	if (!profile) {
		statusMessage_ = "Ray Tracing Profileを読み込めません";
		statusError_ = true;
		return false;
	}
	draft_ = *profile;
	draft_.guid = profileAsset;
	selectedProfile_ = profileAsset;
	selectedEffectIndex_ = draft_.effects.empty() ? -1 : 0;
	dirty_ = false;
	statusMessage_ = "読み込みました";
	statusError_ = false;
	return true;
}

bool Engine::RayTracingEditorTool::SaveProfile(
	const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || !selectedProfile_) {
		return false;
	}
	std::unordered_set<std::string> effectNames;
	for (const RayTracingEffectSettings& effect : draft_.effects) {
		if (effect.name.empty()) {
			statusMessage_ = "エフェクト名を入力してください";
			statusError_ = true;
			return false;
		}
		if (!effectNames.emplace(effect.name).second) {
			statusMessage_ = "エフェクト名が重複しています: " + effect.name;
			statusError_ = true;
			return false;
		}
	}
	const std::filesystem::path path =
		assetDatabase->ResolveFullPath(selectedProfile_);
	if (path.empty() || !JsonAdapter::SaveCanonical(path, ToJson(draft_), 2)) {
		statusMessage_ = "保存に失敗しました";
		statusError_ = true;
		return false;
	}
	assetDatabase->RefreshDependencies(selectedProfile_);
	if (context.panelContext && context.panelContext->renderPipeline) {
		context.panelContext->renderPipeline->ReloadAsset(
			*assetDatabase, selectedProfile_);
	}
	dirty_ = false;
	statusMessage_ = "保存して実行中の設定を更新しました";
	statusError_ = false;
	Logger::Output(LogType::Engine, spdlog::level::info,
		"[RayTracingEditor] Saved profile: {}", path.string());
	return true;
}

void Engine::RayTracingEditorTool::SetDirty() {

	dirty_ = true;
	statusMessage_.clear();
	statusError_ = false;
}
