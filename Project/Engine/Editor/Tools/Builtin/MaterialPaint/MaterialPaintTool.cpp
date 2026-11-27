#include "MaterialPaintTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintMask.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>

#include <algorithm>
#include <filesystem>

namespace Engine {

	void MaterialPaintTool::EndScenePreview() {

		if (maskDatabase_ && !maskDatabaseLife_.expired()) PublishMaterialPaintMasks(*maskDatabase_, true);
		maskDatabase_ = nullptr;
		ClearPreviews();
		targets_.clear();
		savedPreviews_.clear();
		samples_.clear();
		targetIndex_ = batchIndex_ = triangleIndex_ = 0;
		nextRow_ = -1;
		finishingStroke_ = false;
		waitingForMask_ = false;
		if (hoverPicker_) hoverPicker_->Finalize();
		hoverPicker_.reset();
		hoverEntity_ = Entity::Null();
		hoverPending_ = pendingPress_ = false;
		preparingStroke_ = false;
		stroke_ = hasLastPoint_ = false;
		strokeView_ = 0;
		surface_.Clear();
		previewUploads_.Release();
	}

	void MaterialPaintTool::LoadBrush(const AssetDatabase& database) {

		brush_.shape.clear();
		const auto* meta = database.Find(shape_);
		if (!meta) return;
		TextureFileRequestDesc request{};
		request.assetPath = meta->assetPath;
		request.importSettings = MakeTextureImportSettings(TextureImportPreset::Data);
		request.importSettings.generateMipmaps = false;
		request.previewMaxDimension = 512;
		auto decoded = TextureDecoder::Decode(request);
		DirectX::ScratchImage gray;
		const auto* base = decoded.image.GetImage(0, 0, 0);
		if (!base || (base->format != DXGI_FORMAT_R8_UNORM && FAILED(DirectX::Convert(*base,
			DXGI_FORMAT_R8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, gray)))) {
			diagnostic_ = "ブラシ画像を読み込めません";
			shape_ = {};
			return;
		}
		if (gray.GetPixels()) base = gray.GetImage(0, 0, 0);
		brush_.shapeWidth = static_cast<uint32_t>(base->width);
		brush_.shapeHeight = static_cast<uint32_t>(base->height);
		brush_.shape.resize(base->width * base->height);
		for (size_t y = 0; y < base->height; ++y)
			std::copy_n(base->pixels + y * base->rowPitch, base->width, brush_.shape.data() + y * base->width);
	}

	void MaterialPaintTool::DrawEditorTool(const EditorToolContext& context) {

		if (!openWindow_) {
			if (stroke_ || !world_.expired()) { EndScenePreview(); world_.reset(); }
			return;
		}
		if (EditorWindowPresentation::Begin(MakeWindowName("MaterialPaint").c_str(), &openWindow_) && context.panelContext) {
			MyGUI::ScopedPropertyLabelWidth width("MaterialPaint");
			ImGui::Checkbox("SceneViewでペイント", &paintMode_);
			ImGui::TextDisabled("左ドラッグで塗る   Shift＋左ドラッグで消去");
			ImGui::BeginDisabled(!context.CanEditScene() || stroke_);
			DrawLayers(*context.panelContext);
			ImGui::SeparatorText("ブラシ");
			auto* db = context.toolContext.assetDatabase;
			if (MyGUI::AssetReferenceField("形状", shape_, db, {AssetType::Texture}, {}).valueChanged && db) LoadBrush(*db);
			MyGUI::DragFloat("半径", brush_.radius, {.dragSpeed=0.01f, .minValue=0.001f, .maxValue=1000.0f});
			MyGUI::DragFloat("硬さ", brush_.hardness, {.dragSpeed=0.01f, .minValue=0, .maxValue=1});
			MyGUI::DragFloat("強さ", brush_.strength, {.dragSpeed=0.01f, .minValue=0, .maxValue=1});
			MyGUI::DragFloat("回転", brush_.angle, {.dragSpeed=1.0f, .minValue=-180.0f, .maxValue=180.0f});
			ImGui::EndDisabled();
			if (!diagnostic_.empty()) MyGUI::Text("状態", diagnostic_);
		}
		ImGui::End();
		if (!paintMode_ || !openWindow_ || !context.CanEditScene()) EndScenePreview();
	}
}
