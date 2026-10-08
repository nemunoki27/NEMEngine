#include <Engine/Core/Foundation/Math/Quaternion.h>

using namespace Engine;

//============================================================================
//	Quaternion classMethods
//============================================================================
nlohmann::json Quaternion::ToJson() const {

	// xyzw順を保って保存値を変換
	return nlohmann::json{{"x", x}, {"y", y}, {"z", z}, {"w", w}};
}

Quaternion Quaternion::FromJson(const nlohmann::json& data) {

	// xyzw順を保って保存値を変換
	if (data.empty()) {
		return Quaternion::Identity();
	}
	Quaternion quaternion = Quaternion::Identity();
	if (data.is_array() && data.size() == 4) {
		// 配列もobjectと同じxyzw順で読み込む
		quaternion.x = data[0].get<float>();
		quaternion.y = data[1].get<float>();
		quaternion.z = data[2].get<float>();
		quaternion.w = data[3].get<float>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z") && data.contains("w")) {
		quaternion.x = data.value("x", 0.0f);
		quaternion.y = data.value("y", 0.0f);
		quaternion.z = data.value("z", 0.0f);
		quaternion.w = data.value("w", 1.0f);
	}
	return quaternion;
}
