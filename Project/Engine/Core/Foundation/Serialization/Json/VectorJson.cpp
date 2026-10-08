//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

using namespace Engine;

//============================================================================
//	Vector 保存変換
//============================================================================
nlohmann::json Vector2::ToJson() const {

	// 成分順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}};
}

Vector2 Vector2::FromJson(const nlohmann::json& data) {

	// 成分順を保って保存値を変換
	Vector2 v{};
	if (data.is_array() && data.size() == 2) {
		v.x = data[0].get<float>();
		v.y = data[1].get<float>();
	} else if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
	}
	return v;
}

nlohmann::json Vector2I::ToJson() const {

	// 成分順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}};
}

Vector2I Vector2I::FromJson(const nlohmann::json& data) {

	// 成分順を保って保存値を変換
	Vector2I v{};
	if (data.is_array() && data.size() == 2) {
		v.x = data[0].get<int32_t>();
		v.y = data[1].get<int32_t>();
	} else if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<int32_t>();
		v.y = data["y"].get<int32_t>();
	}
	return v;
}

nlohmann::json Vector3::ToJson() const {

	// 成分順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}, {"z", z}};
}

Vector3 Vector3::FromJson(const nlohmann::json& data) {

	// 成分順を保って保存値を変換
	Vector3 v{};
	if (data.is_array() && data.size() == 3) {
		v.x = data[0].get<float>();
		v.y = data[1].get<float>();
		v.z = data[2].get<float>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
		v.z = data["z"].get<float>();
	}
	return v;
}

nlohmann::json Vector3I::ToJson() const {

	// 成分順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}, {"z", z}};
}

Vector3I Vector3I::FromJson(const nlohmann::json& data) {

	// 成分順を保って保存値を変換
	Vector3I v{};
	if (data.is_array() && data.size() == 3) {
		v.x = data[0].get<int32_t>();
		v.y = data[1].get<int32_t>();
		v.z = data[2].get<int32_t>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z")) {
		v.x = data["x"].get<int32_t>();
		v.y = data["y"].get<int32_t>();
		v.z = data["z"].get<int32_t>();
	}
	return v;
}

nlohmann::json Vector4::ToJson() const {

	// 成分順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}, {"z", z}, {"w", w}};
}

Vector4 Vector4::FromJson(const nlohmann::json& data) {

	// 成分順を保って保存値を変換
	Vector4 v{};
	if (data.is_array() && data.size() == 4) {
		v.x = data[0].get<float>();
		v.y = data[1].get<float>();
		v.z = data[2].get<float>();
		v.w = data[3].get<float>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z") && data.contains("w")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
		v.z = data["z"].get<float>();
		v.w = data["w"].get<float>();
	}
	return v;
}
