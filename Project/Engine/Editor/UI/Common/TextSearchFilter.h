#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <string_view>

#include <imgui.h>

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
		// 左端にアイコンを重ねた検索入力を描画する、hintは空欄時のプレースホルダ
		bool DrawInput(const char* id, ImTextureID icon, const char* hint = nullptr);
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

		//--------- variables ----------------------------------------------------

		std::string text_;

		//--------- functions ----------------------------------------------------

		// 前後の空白と大文字小文字の違いを取り除く
		static std::string Normalize(std::string_view text);
	};

	//============================================================================
	//	TextSearchFilter inlineMethods
	//============================================================================

	inline bool TextSearchFilter::IsActive() const { return !Normalize(text_).empty(); }

	inline void TextSearchFilter::Clear() { text_.clear(); }

} // Engine
