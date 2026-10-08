#include "CollisionSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// c++
#include <algorithm>
#include <utility>

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

bool Engine::CollisionSettings::Load() {

	// 初回の失敗でも既定の衝突設定を使えるようにする
	if (!loaded_) {
		ResetDefault();
		drawCollisionWorld_ = false;
		loaded_ = true;
	}
	// 読込完了まで現在の設定を維持する
	CollisionSettings next = *this;
	next.ResetDefault();
	std::error_code error;
	const bool exists = !settingsPath_.empty() && std::filesystem::exists(settingsPath_, error);
	if (error) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "衝突設定を確認できません path={} 内容={}",
			settingsPath_.string(), error.message());
		return false;
	}

	// ファイルがない場合は既定値を使う
	if (exists) {

		nlohmann::json data;
		std::string diagnostic;
		if (!JsonAdapter::TryLoad(settingsPath_, data, &diagnostic) || !data.is_object()) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "衝突設定を読み込めません path={} 内容={}",
				settingsPath_.string(), diagnostic);
			return false;
		}
		try {

			next.drawCollisionWorld_ = data.value("drawCollisionWorld", false);
			next.queriesHitTriggers_ = data.value("queriesHitTriggers", true);

			// Collisionタイプを読み込む
			next.types_.clear();
			if (data.contains("types") && data["types"].is_array()) {
				for (const auto& typeJson : data["types"]) {
					if (next.types_.size() >= kMaxCollisionTypes) {
						break;
					}
					CollisionTypeDefinition type{};
					type.name = typeJson.value("name", "CollisionType");
					type.enabled = typeJson.value("enabled", true);
					next.types_.push_back(type);
				}
			}
			if (next.types_.empty()) {
				next.types_.push_back({ "Default", true });
			}

			// Collision Matrixを読み込む
			next.matrixRows_.fill(0);
			if (data.contains("matrixRows") && data["matrixRows"].is_array()) {
				const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(data["matrixRows"].size()), kMaxCollisionTypes);
				for (uint32_t i = 0; i < count; ++i) {
					next.matrixRows_[i] = data["matrixRows"][i].get<uint32_t>();
				}
			} else {
				for (uint32_t i = 0; i < next.GetTypeCount(); ++i) {
					next.matrixRows_[i] = (next.GetTypeCount() >= kMaxCollisionTypes) ? 0xFFFFFFFFu : ((1u << next.GetTypeCount()) - 1u);
				}
			}
		} catch (const nlohmann::json::exception& exception) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "衝突設定の値が不正です path={} 内容={}",
				settingsPath_.string(), exception.what());
			return false;
		}
	}

	// 解析した設定をまとめて差し替える
	next.TrimMatrix();
	next.loaded_ = true;
	*this = std::move(next);
	return true;
}

bool Engine::CollisionSettings::Save() const {

	nlohmann::json data = nlohmann::json::object();

	data["drawCollisionWorld"] = drawCollisionWorld_;
	data["queriesHitTriggers"] = queriesHitTriggers_;

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
	if (settingsPath_.empty()) {
		return false;
	}
	// 保存の完了を呼出元へ返す
	const bool saved = JsonAdapter::Save(settingsPath_, data);
	if (!saved) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "衝突設定を保存できません path={}", settingsPath_.string());
	}
	return saved;
}

void Engine::CollisionSettings::SetQueriesHitTriggers(bool enabled) {

	EnsureLoaded();
	if (queriesHitTriggers_ == enabled) {
		return;
	}
	queriesHitTriggers_ = enabled;
}

void Engine::CollisionSettings::BindGlobal() {

	SetActiveSettingsPath(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kCollisionSettings));
}

void Engine::CollisionSettings::SetActiveSettingsPath(const std::filesystem::path& settingsPath) {

	const std::filesystem::path nextPath = settingsPath.empty() ? std::filesystem::path{} : settingsPath.lexically_normal();
	if (settingsPath_ == nextPath && loaded_) {
		return;
	}

	settingsPath_ = nextPath;
	loaded_ = false;
	Load();
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

void Engine::CollisionSettings::RemoveType(uint32_t index) {

	EnsureLoaded();
	// 最低1つは残す、範囲外は無視する
	if (index >= types_.size() || types_.size() <= 1) {
		return;
	}

	// 詰める前のペア衝突可否を退避し、indexを除いて行列を作り直す
	const std::array<uint32_t, kMaxCollisionTypes> oldRows = matrixRows_;
	types_.erase(types_.begin() + index);

	matrixRows_.fill(0);
	const uint32_t newCount = GetTypeCount();
	for (uint32_t y = 0; y < newCount; ++y) {

		// 削除indexをまたぐ位置は元の行/列を1つ繰り上げて引き継ぐ
		const uint32_t srcY = (y < index) ? y : y + 1;
		for (uint32_t x = 0; x < newCount; ++x) {

			const uint32_t srcX = (x < index) ? x : x + 1;
			if ((oldRows[srcY] & MakeCollisionTypeBit(srcX)) != 0) {
				matrixRows_[y] |= MakeCollisionTypeBit(x);
			}
		}
	}

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

	queriesHitTriggers_ = true;
	types_.clear();
	types_.push_back({ "Default", true });
	matrixRows_.fill(0);
	matrixRows_[0] = 1u;
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
