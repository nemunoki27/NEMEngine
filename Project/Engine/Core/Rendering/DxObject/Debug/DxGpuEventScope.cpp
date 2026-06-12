#include "DxGPUEventScope.h"

// engine
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

//============================================================================
//	DxGPUEventScope classMethods
//============================================================================

namespace Engine {

	DxGPUEventScope::DxGPUEventScope(ID3D12GraphicsCommandList* commandList, std::string_view label)
		: commandList_(commandList) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			commandList_->BeginEvent(0, label.data(), static_cast<UINT>(label.size()));
		}
#else
		(void)label;
#endif
	}

	DxGPUEventScope::DxGPUEventScope(ID3D12GraphicsCommandList* commandList, const wchar_t* label)
		: commandList_(commandList) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			commandList_->BeginEvent(0, label, static_cast<UINT>((wcslen(label) + 1) * sizeof(wchar_t)));
		}
#else
		(void)label;
#endif
	}

	DxGPUEventScope::~DxGPUEventScope() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (commandList_) {
			commandList_->EndEvent();
		}
#endif
	}

}
