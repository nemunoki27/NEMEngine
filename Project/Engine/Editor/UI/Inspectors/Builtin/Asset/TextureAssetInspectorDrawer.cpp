#include "TextureAssetInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

// c++
#include <string>

//============================================================================
//	TextureAssetInspectorDrawer internal
//============================================================================
namespace {

	// アセットパスの命名から用途ラベルを推測する
	const char* GuessTextureUsageLabel(const std::string& assetPath) {

		std::string lower = assetPath;
		for (char& ch : lower) {
			if (ch >= 'A' && ch <= 'Z') {
				ch = static_cast<char>(ch + ('a' - 'A'));
			}
		}
		auto has = [&](const char* token) { return lower.find(token) != std::string::npos; };

		// 法線マップ、Sponzaの_ddnなど派生法線命名も拾う
		if (has("normal") || has("ddn") || has("_nrm") || has("_norm")) { return "法線マップ"; }
		// ベースカラー、_diff,diffuse,albedo,basecolor等
		if (has("basecolor") || has("base_color") || has("albedo") || has("diff") || has("_col") || has("_alb") || has("_bc")) { return "ベースカラー"; }
		// メタリック/ラフネス
		if (has("metal") || has("rough") || has("_mr") || has("_orm") || has("_arm")) { return "メタリック/ラフネス"; }
		// スペキュラ
		if (has("specular") || has("_spec") || has("_spc")) { return "スペキュラ"; }
		// 発光
		if (has("emiss") || has("emit")) { return "発光"; }
		// 遮蔽AO
		if (has("occlusion") || has("ambientocclusion") || has("_ao") || has("_occ")) { return "遮蔽(AO)"; }
		// ハイト/ディスプレース
		if (has("height") || has("displace") || has("_disp") || has("_hgt")) { return "ハイト/ディスプレース"; }
		return "不明";
	}
}

//============================================================================
//	TextureAssetInspectorDrawer classMethods
//============================================================================
void Engine::TextureAssetInspectorDrawer::Draw(const EditorPanelContext& context, const AssetMeta& meta) {

	// プレビュー用テクスチャを解決する、GUIプレビューと同じキーを共有する
	auto& texService = context.graphicsCore->GetTextureUploadService();
	const std::string previewKey = "gui:texture:preview:" + meta.assetPath;
	if (texService.GetState(previewKey) == TextureRequestState::None) {

		TextureFileRequestDesc desc{};
		desc.key = previewKey;
		desc.assetPath = meta.assetPath;
		desc.forceSRGB = true;
		texService.RequestTextureFile(desc);
	}
	const GPUTextureResource* tex = texService.GetTexture(previewKey);

	// プレビュー256x256
	if (tex && tex->valid) {

		ImGui::Image(static_cast<ImTextureID>(tex->gpuHandle.ptr), ImVec2(256.0f, 256.0f));
	} else {

		ImGui::TextDisabled("プレビューを読み込み中...");
	}
	ImGui::Separator();

	// テクスチャ情報
	ImGui::TextUnformatted("情報");
	if (tex && tex->valid && tex->resource) {

		const D3D12_RESOURCE_DESC desc = tex->resource->GetDesc();
		const std::string_view formatName = EnumAdapter<DXGI_FORMAT>::ToStringView(desc.Format);
		if (!formatName.empty()) {
			ImGui::Text("Format: %.*s", static_cast<int>(formatName.size()), formatName.data());
		} else {
			ImGui::Text("Format: DXGI_FORMAT(%u)", static_cast<uint32_t>(desc.Format));
		}
		ImGui::Text("サイズ: %llu x %u", static_cast<unsigned long long>(desc.Width), desc.Height);
		ImGui::Text("ミップ数: %u", static_cast<uint32_t>(desc.MipLevels));
	} else {

		ImGui::TextDisabled("情報を取得できません");
	}
	ImGui::Text("タイプ: %s", GuessTextureUsageLabel(meta.assetPath));
}
