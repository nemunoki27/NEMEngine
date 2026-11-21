#include "SeamlessTextureTool.h"
#include "SeamlessTextureImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Assets/Project/ProjectAssetPath.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Utility/EditorImageFile.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

	using namespace Engine;

	std::string UploadPreview(TextureUploadService& uploads, const DirectX::Image& image) {

		DirectX::Blob bytes;
		if (FAILED(DirectX::SaveToDDSMemory(image, DirectX::DDS_FLAGS_NONE, bytes)))
			throw std::runtime_error("プレビューを作成できません");
		TextureFileRequestDesc request{};
		request.assetPath = "SeamlessPreview.dds";
		request.snapshotBytes = std::make_shared<std::string>(
			static_cast<const char*>(bytes.GetBufferPointer()), bytes.GetBufferSize());
		request.importSettings.generateMipmaps = false;
		request.requestedColorSpace = TextureColorSpace::Linear;
		return uploads.RequestSnapshot(std::move(request));
	}

	void DrawTiles(const char* label, TextureUploadService& uploads, const std::string& key, const DirectX::Image& image) {

		ImGui::TextUnformatted(label);
		const float side = std::clamp(ImGui::GetContentRegionAvail().x, 1.0f, 384.0f);
		const float scale = side / static_cast<float>((std::max)(image.width, image.height));
		const ImVec2 size(scale * static_cast<float>(image.width), scale * static_cast<float>(image.height));
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		if (const auto* texture = uploads.GetTexture(key); texture && texture->valid) {
			for (int y = 0; y < 3; ++y) {
				for (int x = 0; x < 3; ++x) {
					ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture->gpuHandle.ptr),
						{ pos.x + size.x * x / 3, pos.y + size.y * y / 3 },
						{ pos.x + size.x * (x + 1) / 3, pos.y + size.y * (y + 1) / 3 });
				}
			}
		}
		ImGui::Dummy(size);
	}
}

void Engine::SeamlessTextureTool::LoadPreview(TextureUploadService& uploads, const AssetMeta* meta) {

	uploads.ReleaseSnapshot(std::exchange(originalPreview_, {}));
	uploads.ReleaseSnapshot(std::exchange(seamlessPreview_, {}));
	thumbnail_.Release();
	message_.clear();
	if (!meta) return;
	if (!RuntimePaths::CanWritePath(RuntimePaths::ResolveAssetPath(meta->assetPath)))
		throw std::runtime_error("書き込み可能なプロジェクトのテクスチャを指定してください");
	thumbnail_ = SeamlessTextureImage::Load(meta->assetPath, 256);
	originalPreview_ = UploadPreview(uploads, *thumbnail_.GetImage(0, 0, 0));
	UpdatePreview(uploads);
}

void Engine::SeamlessTextureTool::UpdatePreview(TextureUploadService& uploads) {

	auto image = SeamlessTextureImage::Blend(*thumbnail_.GetImage(0, 0, 0), blendWidth_);
	auto key = UploadPreview(uploads, *image.GetImage(0, 0, 0));
	uploads.ReleaseSnapshot(std::exchange(seamlessPreview_, std::move(key)));
}

void Engine::SeamlessTextureTool::Save(AssetDatabase& database, const AssetMeta& meta) {

	const auto source = database.ResolveFullPath(meta.guid);
	const auto target = ProjectAssetPath::MakeUniquePath(source.parent_path() / (source.stem().wstring() + L"_Seamless.png"));
	if (target.empty() || !RuntimePaths::CanWritePath(target)) throw std::runtime_error("保存先を決定できません");
	auto original = SeamlessTextureImage::Load(meta.assetPath, 0);
	auto image = SeamlessTextureImage::Blend(*original.GetImage(0, 0, 0), blendWidth_);
	StorageFileUtility::FileIdentity identity;
	if (!StorageFileUtility::WriteBytesWithoutReplacement(target, EditorImageFile::EncodePNG(*image.GetImage(0, 0, 0)), identity))
		throw std::runtime_error("PNGを保存できません");
	const auto path = RuntimePaths::ToAssetPath(target);
	if (!database.ImportOrGet(path, AssetType::Texture))
		throw std::runtime_error("PNGを保存しましたがアセットに登録できません: " + path);
	message_ = "保存しました: " + path;
}

void Engine::SeamlessTextureTool::DrawEditorTool(const EditorToolContext& context) {

	if ((!openWindow_ && !source_) || !context.panelContext || !context.panelContext->graphicsCore) return;
	auto& uploads = context.panelContext->graphicsCore->GetTextureUploadService();
	if (!openWindow_) {
		source_ = {};
		LoadPreview(uploads, nullptr);
		return;
	}
	if (EditorWindowPresentation::Begin(MakeWindowName("SeamlessTexture").c_str(), &openWindow_)) {
		try {
			auto* database = context.toolContext.assetDatabase;
			MyGUI::ScopedPropertyLabelWidth width("SeamlessTexture");
			const bool changed = MyGUI::AssetReferenceField("テクスチャ", source_, database,
				{ AssetType::Texture }, {}).valueChanged;
			const auto* meta = database ? database->Find(source_) : nullptr;
			if (changed) LoadPreview(uploads, meta);

			IntEditSetting setting{};
			setting.minValue = 1;
			setting.maxValue = 50;
			if (MyGUI::DragInt("ブレンド幅 (%)", blendWidth_, setting).valueChanged && thumbnail_.GetPixels()) {
				blendWidth_ = std::clamp(blendWidth_, 1, 50);
				UpdatePreview(uploads);
			}
			if (meta && thumbnail_.GetPixels()) {
				if (ImGui::BeginTable("##TexturePreviews", 2, ImGuiTableFlags_SizingStretchSame)) {
					const auto& image = *thumbnail_.GetImage(0, 0, 0);
					ImGui::TableNextColumn();
					DrawTiles("元画像", uploads, originalPreview_, image);
					ImGui::TableNextColumn();
					DrawTiles("変換後", uploads, seamlessPreview_, image);
					ImGui::EndTable();
				}
				if (ImGui::Button("PNGを出力")) Save(*database, *meta);
			}
		} catch (const std::exception& error) {
			message_ = error.what();
		}
		if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
	}
	ImGui::End();
}
