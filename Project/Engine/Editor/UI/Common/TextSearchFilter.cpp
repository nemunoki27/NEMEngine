#include "TextSearchFilter.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cctype>
#include <imgui_stdlib.h>

//============================================================================
//	TextSearchFilter classMethods
//============================================================================

namespace Engine {

	bool TextSearchFilter::DrawInput(const char* id) {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

		// 検索入力の高さを20.0fに固定する、フレーム高さはフォント高+上下余白で決まる
		const float inputHeight = 20.0f;
		const float paddingY = (std::max)(0.0f, (inputHeight - ImGui::GetFontSize()) * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, paddingY));
		const bool changed = ImGui::InputText(id, &text_);
		ImGui::PopStyleVar();
		return changed;
	}

	bool TextSearchFilter::DrawInput(const char* id, ImTextureID icon, const char* hint) {

		const ImGuiStyle& style = ImGui::GetStyle();
		const float inputHeight = 20.0f;
		const float paddingY = (std::max)(0.0f, (inputHeight - ImGui::GetFontSize()) * 0.5f);
		// アイコンは枠の高さに収まる正方形にする
		const float iconSize = (std::max)(1.0f, ImGui::GetFontSize());
		const float iconGap = style.ItemInnerSpacing.x;

		// 入力枠の左上を控えておき、後からアイコンを枠内へ重ねて描く
		const ImVec2 framePos = ImGui::GetCursorScreenPos();

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		// テキスト開始位置をアイコン幅ぶん右へずらすため、左のFramePaddingを広げる
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			ImVec2(style.FramePadding.x + iconSize + iconGap, paddingY));
		const bool changed = hint ?
			ImGui::InputTextWithHint(id, hint, &text_) : ImGui::InputText(id, &text_);
		ImGui::PopStyleVar();

		if (icon != ImTextureID{}) {

			const float frameHeight = ImGui::GetFontSize() + paddingY * 2.0f;
			const ImVec2 iconMin(framePos.x + style.FramePadding.x,
				framePos.y + (frameHeight - iconSize) * 0.5f);
			ImGui::GetWindowDrawList()->AddImage(icon, iconMin,
				ImVec2(iconMin.x + iconSize, iconMin.y + iconSize));
		}
		return changed;
	}

	bool TextSearchFilter::Matches(std::string_view text) const {

		const std::string needle = Normalize(text_);
		if (needle.empty()) {
			return true;
		}

		const std::string haystack = Normalize(text);
		return haystack.find(needle) != std::string::npos;
	}

	std::string TextSearchFilter::Normalize(std::string_view text) {

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
}
