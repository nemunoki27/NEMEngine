#include "TextureAssetInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

//============================================================================
//	TextureAssetInspectorDrawer internal
//============================================================================
namespace {

	constexpr float kPreviewSize = 256.0f;
	constexpr float kCheckerSize = 12.0f;

	// アセットパスの命名から用途ラベルを推測する
	const char* GuessTextureUsageLabel(const std::string& assetPath) {

		std::string lower = assetPath;
		for (char& ch : lower) {
			if (ch >= 'A' && ch <= 'Z') {
				ch = static_cast<char>(ch + ('a' - 'A'));
			}
		}
		auto has = [&](const char* token) {

			return lower.find(token) != std::string::npos;
		};

		if (has("normal") || has("ddn") || has("_nrm") || has("_norm")) {
			return "法線マップ";
		}
		if (has("basecolor") || has("base_color") || has("albedo") ||
			has("diff") || has("_col") || has("_alb") || has("_bc")) {
			return "ベースカラー";
		}
		if (has("metal") || has("rough") || has("_mr") ||
			has("_orm") || has("_arm")) {
			return "メタリック/ラフネス";
		}
		if (has("specular") || has("_spec") || has("_spc")) {
			return "スペキュラ";
		}
		if (has("emiss") || has("emit")) {
			return "発光";
		}
		if (has("occlusion") || has("ambientocclusion") ||
			has("_ao") || has("_occ")) {
			return "遮蔽(AO)";
		}
		if (has("height") || has("displace") || has("_disp") || has("_hgt")) {
			return "ハイト/ディスプレース";
		}
		return "不明";
	}

	// プレビュー背景へチェッカーを描画する
	void DrawCheckerboard(ImDrawList* drawList,
		const ImVec2& min, const ImVec2& max) {

		const ImU32 colors[2]{
			IM_COL32(64, 64, 64, 255),
			IM_COL32(104, 104, 104, 255),
		};
		uint32_t row = 0;
		for (float y = min.y; y < max.y; y += kCheckerSize, ++row) {

			uint32_t column = 0;
			for (float x = min.x; x < max.x; x += kCheckerSize, ++column) {

				drawList->AddRectFilled(
					ImVec2(x, y),
					ImVec2((std::min)(x + kCheckerSize, max.x),
						(std::min)(y + kCheckerSize, max.y)),
					colors[(row + column) & 1]);
			}
		}
	}
}

//============================================================================
//	TextureAssetInspectorDrawer classMethods
//============================================================================
void Engine::TextureAssetInspectorDrawer::Draw(
	const EditorPanelContext& context, const AssetMeta& meta) {

	SyncSelection(meta);
	DrawPreview(context, meta);
	ImGui::Spacing();
	DrawImportSettings(context, meta);
}

void Engine::TextureAssetInspectorDrawer::SyncSelection(const AssetMeta& meta) {

	if (selectedAsset_ == meta.guid) {
		return;
	}
	selectedAsset_ = meta.guid;
	savedSettings_ = ParseTextureImportSettings(meta.importerSettings);
	draftSettings_ = savedSettings_;
	statusMessage_.clear();
}

void Engine::TextureAssetInspectorDrawer::DrawImportSettings(
	const EditorPanelContext& context, const AssetMeta& meta) {

	ImGui::SeparatorText("インポート設定");
	MyGUI::ScopedPropertyLabelWidth labelWidth("TextureImportSettings");
	const TextureImportPreset previousPreset = draftSettings_.preset;
	if (MyGUI::EnumCombo("プリセット", draftSettings_.preset).valueChanged &&
		previousPreset != draftSettings_.preset) {

		draftSettings_ = MakeTextureImportSettings(draftSettings_.preset);
	}
	MyGUI::EnumCombo("色空間", draftSettings_.colorSpace);
	MyGUI::EnumCombo("フィルタ", draftSettings_.filter);
	MyGUI::EnumCombo("アドレスU", draftSettings_.addressU);
	MyGUI::EnumCombo("アドレスV", draftSettings_.addressV);
	MyGUI::Checkbox("ミップを生成", draftSettings_.generateMipmaps);

	int32_t maxAnisotropy = static_cast<int32_t>(draftSettings_.maxAnisotropy);
	ImGui::BeginDisabled(draftSettings_.filter != TextureFilterMode::Anisotropic);
	if (MyGUI::DragInt("最大異方性", maxAnisotropy,
		{ .dragSpeed = 1.0f,.minValue = 1,.maxValue = 16 }).valueChanged) {

		draftSettings_.maxAnisotropy = static_cast<uint32_t>((std::clamp)(
			maxAnisotropy, 1, 16));
	}
	ImGui::EndDisabled();
	MyGUI::EnumCombo("法線規約", draftSettings_.normalConvention);
	MyGUI::Checkbox("透過色ブリード", draftSettings_.alphaColorBleed);

	const bool dirty = draftSettings_ != savedSettings_;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("適用", ImVec2(width, 0.0f))) {

		AssetDatabase* database = context.editorContext ?
			context.editorContext->assetDatabase : nullptr;
		if (database && database->UpdateImporterSettings(
			meta.guid, ToJson(draftSettings_), kTextureImporterVersion)) {

			const std::filesystem::path fullPath = database->ResolveFullPath(meta.guid);
			context.graphicsCore->GetTextureUploadService().RequestReloadByFile(
				fullPath, &draftSettings_);
			savedSettings_ = draftSettings_;
			statusMessage_ = "適用しました";
		} else {

			statusMessage_ = "適用に失敗しました";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("元に戻す", ImVec2(width, 0.0f))) {

		draftSettings_ = savedSettings_;
		statusMessage_.clear();
	}
	ImGui::EndDisabled();
	if (!statusMessage_.empty()) {
		ImGui::TextDisabled("%s", statusMessage_.c_str());
	}
}

void Engine::TextureAssetInspectorDrawer::DrawPreview(
	const EditorPanelContext& context, const AssetMeta& meta) {

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("TexturePreviewSettings");
		MyGUI::EnumCombo("表示色空間", previewColorSpace_);
		MyGUI::EnumCombo("表示チャンネル", previewChannel_);
	}

	TextureUploadService& textureService =
		context.graphicsCore->GetTextureUploadService();
	const std::string previewKey = std::format(
		"gui:texture:preview:{}:{}:{}", ToString(meta.guid),
		static_cast<uint32_t>(previewColorSpace_),
		static_cast<uint32_t>(previewChannel_));
	if (textureService.GetState(previewKey) == TextureRequestState::None) {

		TextureFileRequestDesc desc{};
		desc.key = previewKey;
		desc.assetPath = meta.assetPath;
		desc.importSettings = savedSettings_;
		desc.requestedColorSpace = previewColorSpace_;
		desc.overrideImportColorSpace = true;
		desc.previewChannel = previewChannel_;
		textureService.RequestTextureFile(desc);
	}
	const GPUTextureResource* texture = textureService.GetTexture(previewKey);
	if (!texture || !texture->valid || !texture->resource) {

		ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize));
		ImGui::TextDisabled("プレビューを読み込み中...");
		return;
	}

	const D3D12_RESOURCE_DESC resourceDesc = texture->resource->GetDesc();
	const float aspect = static_cast<float>(resourceDesc.Width) /
		static_cast<float>((std::max)(1u, resourceDesc.Height));
	ImVec2 imageSize(kPreviewSize, kPreviewSize);
	if (1.0f < aspect) {
		imageSize.y = kPreviewSize / aspect;
	} else {
		imageSize.x = kPreviewSize * aspect;
	}

	const ImVec2 regionMin = ImGui::GetCursorScreenPos();
	const ImVec2 imageMin(
		regionMin.x + (kPreviewSize - imageSize.x) * 0.5f,
		regionMin.y + (kPreviewSize - imageSize.y) * 0.5f);
	const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	DrawCheckerboard(drawList, imageMin, imageMax);

	const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
	const bool nearest = savedSettings_.filter == TextureFilterMode::Point;
	const ImDrawCallback setSampler = nearest ?
		platformIO.DrawCallback_SetSamplerNearest :
		platformIO.DrawCallback_SetSamplerLinear;
	if (setSampler) {
		drawList->AddCallback(setSampler);
	}
	drawList->AddImage(static_cast<ImTextureID>(texture->gpuHandle.ptr),
		imageMin, imageMax);
	if (setSampler && platformIO.DrawCallback_SetSamplerLinear) {
		drawList->AddCallback(platformIO.DrawCallback_SetSamplerLinear);
	}
	ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize));
	ImGui::Separator();

	const std::string_view formatName = EnumAdapter<DXGI_FORMAT>::ToStringView(
		resourceDesc.Format);
	if (!formatName.empty()) {
		ImGui::Text("Format: %.*s", static_cast<int>(formatName.size()),
			formatName.data());
	} else {
		ImGui::Text("Format: DXGI_FORMAT(%u)",
			static_cast<uint32_t>(resourceDesc.Format));
	}
	ImGui::Text("サイズ: %llu x %u",
		static_cast<unsigned long long>(resourceDesc.Width), resourceDesc.Height);
	ImGui::Text("ミップ数: %u", static_cast<uint32_t>(resourceDesc.MipLevels));
	ImGui::Text("推測用途: %s", GuessTextureUsageLabel(meta.assetPath));
}
