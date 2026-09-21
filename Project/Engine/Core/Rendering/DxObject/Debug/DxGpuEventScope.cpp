#include "DxGPUEventScope.h"

// engine
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
// c++
#include <string>
// pix
#include <WinPixEventRuntime/pix3.h>

//============================================================================
//	DxGPUEventScope classMethods
//============================================================================

namespace Engine {

	DxGPUEventScope::DxGPUEventScope(ID3D12GraphicsCommandList* commandList, [[maybe_unused]] std::string_view label)
		: commandList_(commandList) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			const std::string text(label);
			PIXBeginEvent(commandList_, PIX_COLOR_DEFAULT, "%s", text.c_str());
		}
#endif
	}

	DxGPUEventScope::DxGPUEventScope(ID3D12GraphicsCommandList* commandList, [[maybe_unused]] const wchar_t* label)
		: commandList_(commandList) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			PIXBeginEvent(commandList_, PIX_COLOR_DEFAULT, L"%ls", label);
		}
#endif
	}

	DxGPUEventScope::~DxGPUEventScope() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			PIXEndEvent(commandList_);
		}
#endif
	}

}