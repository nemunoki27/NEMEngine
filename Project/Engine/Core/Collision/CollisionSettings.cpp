#include "CollisionSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Utility/Json/JsonAdapter.h>

// c++
#include <algorithm>

namespace {

	// Project直下に保存するCollision設定ファイル
	constexpr const char* kSettingsRelativePath = "GameAssets/Collision/collisionSettings.json";
}

//============================================================================
//	CollisionSettings classMethods
//============================================================================

Engine::CollisionSettings& Engine::CollisionSettings::GetInstance() {

	static CollisionSettings instance;
	return instance;
}

void Engine::CollisionSettings::EnsureLoaded() {

	if (!loaded_) {
		Load();
	}
}

void Engine::CollisionSettings::Load() {

	settingsPath_ = ResolveSettingsPath();
	ResetDefault();

	// 設定ファイルが存在する場合のみ、デフォルト設定を上書きする
	if (std::filesystem::exists(settingsPath_)) {

		const nlohmann::json data = JsonAdapter::Load(settingsPath_.string(), false);
		if (data.is_object()) {

			// Collisionタイプを読み込む
			types_.clear();
			if (data.contains("types") && data["types"].is_array()) {
				for (const auto& typeJson : data["types"]) {
					if (types_.size() >= kMaxCollisionTypes) {
						break;
					}
					CollisionTypeDefinition type{};
					type.name = typeJson.value("name", "CollisionType");
					type.enabled = typeJson.value("enabled", true);
					types_.push_back(type);
				}
			}
			if (types_.empty()) {
				types_.push_back({ "Default", true });
			}

			// Collision Matrixを読み込む
			matrixRows_.fill(0);
			if (data.contains("matrixRows") && data["matrixRows"].is_array()) {
				const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(data["matrixRows"].size()), kMaxCollisionTypes);
				for (uint32_t i = 0; i < count; ++i) {
					matrixRows_[i] = data["matrixRows"][i].get<uint32_t>();
				}
			} else {
				for (uint32_t i = 0; i < GetTypeCount(); ++i) {
					matrixRows_[i] = (GetTypeCount() >= kMaxCollisionTypes) ? 0xFFFFFFFFu : ((1u << GetTypeCount()) - 1u);
				}
			}
		}
	}

	TrimMatrix();
	loaded_ = true;
}

void Engine::CollisionSettings::Save() const {

	nlohmann::json data = nlohmann::json::object();

	// Collisionタイプを書き出す
	data["types"] = nlohmann::json::array();
	for (const auto& type : types_) {
		data["types"].push_back({
			{ "name", type.name },
			{ "enabled", type.enabled },
			});
	}

	// Collision Matrixを書き出す
	data["matrixRows"] = nlohmann::json::array();
	for (uint32_t i = 0; i < kMaxCollisionTypes; ++i) {
		data["matrixRows"].push_back(matrixRows_[i]);
	}
	JsonAdapter::Save(settingsPath_.empty() ? ResolveSettingsPath().string() : settingsPath_.string(), data);
}

bool Engine::CollisionSettings::AddType(const std::string& name) {

	EnsureLoaded();
	if (types_.size() >= kMaxCollisionTypes) {
		return false;
	}

	const uint32_t newIndex = static_cast<uint32_t>(types_.size());
	types_.push_back({ name.empty() ? ("CollisionType" + std::to_string(newIndex)) : name, true });

	// 新しいタイプは既存タイプすべてと衝突する設定にする
	for (uint32_t i = 0; i < GetTypeCount(); ++i) {
		SetPairEnabled(i, newIndex, true);
	}
	Save();
	return true;
}

void Engine::CollisionSettings::RemoveLastType() {

	EnsureLoaded();
	if (types_.size() <= 1) {
		return;
	}
	types_.pop_back();
	TrimMatrix();
	Save();
}

void Engine::CollisionSettings::SetTypeName(uint32_t index, const std::string& name) {

	EnsureLoaded();
	if (index >= types_.size() || name.empty()) {
		return;
	}
	types_[index].name = name;
	Save();
}

void Engine::CollisionSettings::SetTypeEnabled(uint32_t index, bool enabled) {

	EnsureLoaded();
	if (index >= types_.size()) {
		return;
	}
	types_[index].enabled = enabled;
	Save();
}

void Engine::CollisionSettings::SetPairEnabled(uint32_t typeA, uint32_t typeB, bool enabled) {

	if (typeA >= kMaxCollisionTypes || typeB >= kMaxCollisionTypes) {
		return;
	}
	const uint32_t bitA = MakeCollisionTypeBit(typeA);
	const uint32_t bitB = MakeCollisionTypeBit(typeB);
	if (enabled) {
		matrixRows_[typeA] |= bitB;
		matrixRows_[typeB] |= bitA;
	} else {
		matrixRows_[typeA] &= ~bitB;
		matrixRows_[typeB] &= ~bitA;
	}
}

bool Engine::CollisionSettings::IsPairEnabled(uint32_t typeA, uint32_t typeB) const {

	if (typeA >= types_.size() || typeB >= types_.size()) {
		return false;
	}
	if (!types_[typeA].enabled || !types_[typeB].enabled) {
		return false;
	}
	return (matrixRows_[typeA] & MakeCollisionTypeBit(typeB)) != 0;
}

bool Engine::CollisionSettings::CanCollide(uint32_t typeMaskA, uint32_t typeMaskB) const {

	const uint32_t count = static_cast<uint32_t>(types_.size());
	for (uint32_t typeA = 0; typeA < count; ++typeA) {
		if (!HasCollisionType(typeMaskA, typeA)) {
			continue;
		}
		for (uint32_t typeB = 0; typeB < count; ++typeB) {
			if (HasCollisionType(typeMaskB, typeB) && IsPairEnabled(typeA, typeB)) {
				return true;
			}
		}
	}
	return false;
}

void Engine::CollisionSettings::ResetDefault() {

	types_.clear();
	types_.push_back({ "Default", true });
	matrixRows_.fill(0);
	matrixRows_[0] = 1u;
}

std::filesystem::path Engine::CollisionSettings::ResolveSettingsPath() const {

	return RuntimePaths::GetProjectRoot() / kSettingsRelativePath;
}

void Engine::CollisionSettings::TrimMatrix() {

	const uint32_t count = GetTypeCount();
	const uint32_t validMask = (count >= kMaxCollisionTypes) ? 0xFFFFFFFFu : ((1u << count) - 1u);
	for (uint32_t i = 0; i < kMaxCollisionTypes; ++i) {
		if (i < count) {
			matrixRows_[i] &= validMask;
		} else {
			matrixRows_[i] = 0;
		}
	}
}
