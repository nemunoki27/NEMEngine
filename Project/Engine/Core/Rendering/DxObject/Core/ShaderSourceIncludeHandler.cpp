#include "ShaderSourceIncludeHandler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
// c++
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <exception>

namespace {

	// Windowsのパス比較用に表記を合わせる
	std::wstring MakeShaderSourcePathKey(const std::filesystem::path& path) {

		std::error_code error;
		const auto resolved = std::filesystem::weakly_canonical(path, error);
		if (error) { return {}; }
		std::wstring key = resolved.generic_wstring();
		std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
			return static_cast<wchar_t>(std::towlower(value));
		});
		return key;
	}

	//============================================================================
	//	ShaderSourceIncludeHandler class
	//	Cookの入力root内に限ってIncludeを読み込む
	//============================================================================
	class ShaderSourceIncludeHandler final : public IDxcIncludeHandler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderSourceIncludeHandler(IDxcIncludeHandler* handler, const std::filesystem::path& root) : handler_(handler), root_(root) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceID, void** result) override {

			if (!result) { return E_POINTER; }
			*result = nullptr;
			if (interfaceID != __uuidof(IUnknown) && interfaceID != __uuidof(IDxcIncludeHandler)) { return E_NOINTERFACE; }
			*result = static_cast<IDxcIncludeHandler*>(this);
			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override {

			return ++referenceCount_;
		}

		ULONG STDMETHODCALLTYPE Release() override {

			const ULONG count = --referenceCount_;
			if (count == 0) { delete this; }
			return count;
		}

		HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** source) override {

			if (!source) { return E_POINTER; }
			*source = nullptr;
			if (!fileName) { return E_INVALIDARG; }
			try {
				// 親階層を畳み、長いパスをWindows形式へ揃える
				const auto normalized = std::filesystem::path(fileName).lexically_normal();
				if (!root_.empty() && !Engine::IsShaderSourceWithinRoot(normalized, root_)) { return E_ACCESSDENIED; }
				const auto native = Engine::Algorithm::ToFileSystemPath(normalized);
				return handler_->LoadSource(native.c_str(), source);
			} catch (const std::exception&) { return E_FAIL; }
		}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::atomic<ULONG> referenceCount_ = 1;
		ComPtr<IDxcIncludeHandler> handler_;
		std::filesystem::path root_;
	};
}

bool Engine::IsShaderSourceWithinRoot(const std::filesystem::path& source, const std::filesystem::path& root) {

	const std::wstring sourceKey = MakeShaderSourcePathKey(source);
	std::wstring rootKey = MakeShaderSourcePathKey(root);
	if (sourceKey.empty() || rootKey.empty()) { return false; }
	if (rootKey.back() != L'/') { rootKey.push_back(L'/'); }
	return sourceKey.starts_with(rootKey);
}

ComPtr<IDxcIncludeHandler> Engine::CreateShaderSourceIncludeHandler(IDxcIncludeHandler* handler,
	const std::filesystem::path& root) {

	ComPtr<IDxcIncludeHandler> result;
	if (handler) { result.Attach(new ShaderSourceIncludeHandler(handler, root)); }
	return result;
}
