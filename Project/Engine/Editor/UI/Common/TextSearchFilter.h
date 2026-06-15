#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>
#include <imgui_stdlib.h>

// c++
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Engine {

	//============================================================================
	//	TextSearchFilter class
	//	UI一覧の簡易テキスト検索
	//============================================================================
	class TextSearchFilter {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		// ラベルなしの検索入力を描画する
		bool DrawInput(const char* id);
		// 検索文字が入力されているか
		bool IsActive() const;
		// テキストが検索に一致するか
		bool Matches(std::string_view text) const;
		// 検索文字をクリアする
		void Clear();

		const std::string& GetText() const { return text_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		static std::string Normalize(std::string_view text);

		//============================================================================
		//	private Members
		//============================================================================
		std::string text_;
	};

	//============================================================================
	//	TextSearchFilter inlineMethods
	//============================================================================
	inline bool TextSearchFilter::DrawInput(const char* id) {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

		// 検索入力の高さを20.0fに固定する、フレーム高さはフォント高+上下余白で決まる
		const float inputHeight = 20.0f;
		const float paddingY = (std::max)(0.0f, (inputHeight - ImGui::GetFontSize()) * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, paddingY));
		const bool changed = ImGui::InputText(id, &text_);
		ImGui::PopStyleVar();
		return changed;
	}

	inline bool TextSearchFilter::IsActive() const {

		return !Normalize(text_).empty();
	}

	inline bool TextSearchFilter::Matches(std::string_view text) const {

		const std::string needle = Normalize(text_);
		if (needle.empty()) {
			return true;
		}

		const std::string haystack = Normalize(text);
		return haystack.find(needle) != std::string::npos;
	}

	inline void TextSearchFilter::Clear() {

		text_.clear();
	}

	inline std::string TextSearchFilter::Normalize(std::string_view text) {

		std::string result(text);
		const auto first = std::find_if_not(result.begin(), result.end(), [](unsigned char c) {
			return std::isspace(c) != 0;
		});
		const auto last = std::find_if_not(result.rbegin(), result.rend(), [](unsigned char c) {
			return std::isspace(c) != 0;
		}).base();

		if (first >= last) {
			return {};
		}

		result = std::string(first, last);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}
} // Engine
