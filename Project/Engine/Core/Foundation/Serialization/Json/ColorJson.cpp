#include <Engine/Core/Foundation/Math/Color.h>

using namespace Engine;

//============================================================================
//	Color3 classMethods
//============================================================================
nlohmann::json Color3::ToJson() const {

	// 成分名を付けて保存
	return nlohmann::json{{"r", r}, {"g", g}, {"b", b}};
}

Color3 Color3::FromJson(const nlohmann::json& data) {

	// 配列とobjectから色を読み込む
	Color3 color{};
	if (data.is_array() && data.size() == 3) {
		color.r = data[0].get<float>();
		color.g = data[1].get<float>();
		color.b = data[2].get<float>();
	} else if (data.contains("r") && data.contains("g") && data.contains("b")) {
		color.r = data["r"].get<float>();
		color.g = data["g"].get<float>();
		color.b = data["b"].get<float>();
	}
	return color;
}

//============================================================================
//	Color4 classMethods
//============================================================================
nlohmann::json Color4::ToJson() const {

	// 成分名を付けて保存
	return nlohmann::json{{"r", r}, {"g", g}, {"b", b}, {"a", a}};
}

Color4 Color4::FromJson(const nlohmann::json& data) {

	// 配列とobjectから色を読み込む
	Color4 color{};
	if (data.is_array() && data.size() == 4) {
		color.r = data[0].get<float>();
		color.g = data[1].get<float>();
		color.b = data[2].get<float>();
		color.a = data[3].get<float>();
	} else if (data.contains("r") && data.contains("g") && data.contains("b")) {
		color.r = data.value("r", 0.0f);
		color.g = data.value("g", 0.0f);
		color.b = data.value("b", 0.0f);
		color.a = data.value("a", 1.0f);
	}
	return color;
}
