#pragma once

#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <DirectXTex.h>

namespace Engine {

	class TextureUploadService;
	struct AssetMeta;

	//============================================================================
	//	SeamlessTextureTool class
	//	繰り返しを確認しながらシームレス画像を作成する
	//============================================================================
	class SeamlessTextureTool final : public IEditorTool {
	public:

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
		void DrawEditorTool(const EditorToolContext& context) override;
	private:

		ToolDescriptor descriptor_{
			.id = "engine.seamless_texture", .name = "シームレステクスチャ変換", .category = "テクスチャ",
			.owner = ToolOwner::Engine, .flags = ToolFlags::EditOnly,
		};
		AssetID source_{};
		int32_t blendWidth_ = 15;
		DirectX::ScratchImage thumbnail_;
		std::string originalPreview_;
		std::string seamlessPreview_;
		std::string message_;

		void LoadPreview(TextureUploadService& uploads, const AssetMeta* meta);
		void UpdatePreview(TextureUploadService& uploads);
		void Save(AssetDatabase& database, const AssetMeta& meta);
	};
}
