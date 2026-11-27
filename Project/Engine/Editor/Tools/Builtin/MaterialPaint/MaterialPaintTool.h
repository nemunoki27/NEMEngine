#pragma once

#include "MaterialPaintBrush.h"
#include "MaterialPaintEdit.h"
#include "MaterialPaintSurface.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintAtlas.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshPicker.h>
#include <deque>
#include <filesystem>

namespace Engine {

	class MaterialPaintTool final : public IEditorTool {
	public:
		~MaterialPaintTool() override { EndScenePreview(); }
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
		void DrawEditorTool(const EditorToolContext& context) override;
		void EndScenePreview() override;
		bool DrawScene(const EditorPanelContext& context, ImVec2 origin, ImVec2 size, bool hovered);
	private:
		struct PaintTarget {
			MaterialPaintEdit edit;
			std::shared_ptr<const MaterialPaintAtlas> atlas;
			std::vector<Matrix4x4> matrices;
			std::vector<MaterialPaintAtlas::TriangleBatch> worldBatches;
			std::vector<bool> allowed;
			std::vector<uint8_t> mask;
			uint32_t channel = 0;
			bool changed = false;
			MaterialPaintRegion dirty;
			std::shared_ptr<RenderTexture2D> preview;
			AssetID previewID{};
		};

		ToolDescriptor descriptor_{.id = "engine.material_paint", .name = "マテリアルペイント",
			.category = "レンダリング", .owner = ToolOwner::Engine, .flags = ToolFlags::EditOnly, .order = 0};
		MaterialPaintSurface surface_;
		FrameUploadBufferAllocator previewUploads_;
		std::unique_ptr<MeshSubMeshPicker> hoverPicker_;
		Vector2 hoverPixel_{};
		double nextHoverPick_ = 0.0;
		Entity hoverEntity_ = Entity::Null();
		uint32_t hoverSubMesh_ = 0;
		bool hoverPending_ = false, pendingPress_ = false;
		bool preparingStroke_ = false;
		MaterialPaintBrush brush_;
		AssetID activeMaterial_{}, layerMaterial_{}, shape_{};
		std::vector<AssetID> palette_;
		std::filesystem::path palettePath_;
		bool paintMode_ = true, stroke_ = false, hasLastPoint_ = false;
		bool waitingForMask_ = false;
		AssetDatabase* maskDatabase_ = nullptr;
		std::weak_ptr<const uint8_t> maskDatabaseLife_;
		uint32_t resolution_ = 1024;
		int baseSlot_ = 0;
		Vector2 lastPoint_{}, pressPoint_{};
		bool pressErase_ = false;
		std::string diagnostic_;
		std::vector<PaintTarget> targets_;
		struct BrushSample {
			Vector3 point, normal;
			bool erase;
		};
		std::deque<BrushSample> samples_;
		size_t targetIndex_ = 0, batchIndex_ = 0;
		uint32_t triangleIndex_ = 0;
		int nextRow_ = -1;
		bool finishingStroke_ = false;
		struct SavedPreview {
			AssetID mask;
			std::shared_ptr<RenderTexture2D> texture;
		};
		std::vector<SavedPreview> savedPreviews_;
		std::weak_ptr<const ECSWorldLifetime> world_;
		const ECSWorld* previewWorld_ = nullptr;
		ImGuiID strokeView_ = 0;
		void DrawLayers(const EditorPanelContext& context);
		void DrawPalette(const EditorPanelContext& context, AssetDatabase& database);
		void SavePalette();
		const char* HoverDiagnostic(const EditorPanelContext& context, Entity object, uint32_t subMesh);
		void UpdateHover(const EditorPanelContext& context, Vector2 pixel);
		void BeginStroke(const EditorPanelContext& context, bool erase);
		void FinishStroke(const EditorPanelContext& context);
		void UpdatePreviews(const EditorPanelContext& context);
		void ApplyBrushSamples(const EditorPanelContext& context);
		void ResolveCommittedMasks(const EditorPanelContext& context);
		void ClearPreviews();
		void LoadBrush(const AssetDatabase& database);
	};
}
