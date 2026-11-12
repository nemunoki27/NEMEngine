#include "EditorManualNavigation.h"

//============================================================================
//	include
//============================================================================
#include "EditorShell.h"

// c++
#include <cctype>

namespace {

	constexpr std::string_view kManualURL = "https://nemengine-docs-9395b949dc74f68009ec.pages.dev";
}

//============================================================================
//	EditorManualNavigation methods
//============================================================================
std::string Engine::EditorManualNavigation::GetComponentPage(std::string_view component) {

	if (component == "Script") return "/manual/scripting";
	std::string slug;
	for (size_t i = 0; i < component.size(); ++i) {
		const auto value = static_cast<unsigned char>(component[i]);
		const bool boundary = i && std::isupper(value) &&
			(std::islower(static_cast<unsigned char>(component[i - 1])) || std::isdigit(static_cast<unsigned char>(component[i - 1])) ||
				(std::isupper(static_cast<unsigned char>(component[i - 1])) && i + 1 < component.size() &&
					std::islower(static_cast<unsigned char>(component[i + 1]))));
		if (boundary) slug += '-';
		slug += static_cast<char>(std::tolower(value));
	}
	return "/components/" + slug;
}

bool Engine::EditorManualNavigation::OpenComponent(std::string_view component) {

	return EditorShell::OpenURL(std::string(kManualURL) + GetComponentPage(component));
}
