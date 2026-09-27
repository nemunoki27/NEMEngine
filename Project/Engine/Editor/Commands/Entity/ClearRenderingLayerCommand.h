#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	ClearRenderingLayerCommand class
	//	指定Layerの割当をScene内から解除するコマンド
	//============================================================================
	class ClearRenderingLayerCommand final :
		public IEditorCommand {
	public:
		ClearRenderingLayerCommand(uint32_t layerIndex);
		~ClearRenderingLayerCommand() override = default;

		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;
		const char* GetName() const override { return "Clear Rendering Layer"; }

	private:
		enum class TargetKind : uint8_t {
		Mesh,
		Sprite,
		Primitive,
		Text,
		Line,
		DirectionalLight,
		PointLight,
		SpotLight,
		RectLight,
		};

		struct Entry {
			UUID entity{};
			TargetKind kind = TargetKind::Mesh;
			uint32_t previousMask = 0;
		};

		uint32_t layerIndex_ = 0;
		std::vector<Entry> entries_{};
		bool captured_ = false;

		bool Apply(EditorCommandContext& context, bool clearBit);
	};
} // Engine
