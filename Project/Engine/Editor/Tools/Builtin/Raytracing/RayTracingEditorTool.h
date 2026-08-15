#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Raytracing/RayTracingProfileAsset.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	RayTracingEditorTool class
	//	シーンごとのDispatchRays構成を編集するツール
	//============================================================================
	class RayTracingEditorTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RayTracingEditorTool() = default;
		~RayTracingEditorTool() override = default;

		void Tick(ToolContext& context) override;
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		void OpenAsset(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.ray_tracing",
			.name = "レイトレーシング設定",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 10,
		};

		bool openWindow_ = false;
		bool dirty_ = false;
		AssetID selectedProfile_{};
		AssetID requestedProfile_{};
		AssetID observedSceneProfile_{};
		int32_t selectedEffectIndex_ = -1;
		RayTracingProfileAsset draft_{};
		std::string statusMessage_{};
		bool statusError_ = false;

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);
		void DrawEffectList();
		void DrawEffectDetail(const EditorToolContext& context);
		void DrawResourceBindings(const EditorToolContext& context,
			RayTracingEffectSettings& effect,
			const ShaderReflectionInfo* reflection);
		void DrawParameterOverrides(RayTracingEffectSettings& effect,
			const ShaderReflectionInfo* reflection);
		bool LoadProfile(const EditorToolContext& context, AssetID profileAsset);
		bool SaveProfile(const EditorToolContext& context);
		void SetDirty();
	};
} // Engine
