#include "MaterialParameter.h"

//============================================================================
//	include
//============================================================================
#include "MaterialParameterHash.h"
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <algorithm>

namespace {

	// ID順の領域から一致する値を探す
	template<typename Records>
	auto FindParameterRecord(Records& records, Engine::MaterialParameterID id) {

		const auto position = std::lower_bound(records.begin(), records.end(), id.value,
			[](const Engine::MaterialParameterRecord& record, uint64_t target) { return record.id.value < target; });
		return position != records.end() && position->id == id ? &*position : nullptr;
	}

	// IDが重なる場合は名前も照合する
	template<typename Records>
	auto FindParameterRecord(Records& records, Engine::MaterialParameterID id, std::string_view name) {

		auto position = std::lower_bound(records.begin(), records.end(), id.value,
			[](const Engine::MaterialParameterRecord& record, uint64_t target) { return record.id.value < target; });
		for (; position != records.end() && position->id == id; ++position) {
			if (position->namedValue.first == name) { return &*position; }
		}
		return static_cast<decltype(&*position)>(nullptr);
	}
}

//============================================================================
//	MaterialParameterSet classMethods
//============================================================================
Engine::MaterialParameterSet::MaterialParameterSet(const MaterialParameterSet& other) {

	// コピー元の値を別領域へ複製する
	if (other.data_) {
		data_ = std::make_unique<Data>(*other.data_);
	}
}

Engine::MaterialParameterSet& Engine::MaterialParameterSet::operator=(const MaterialParameterSet& other) {

	// 自分への代入では所有を変更しない
	if (this == &other) {
		return *this;
	}
	data_ = other.data_ ? std::make_unique<Data>(*other.data_) : nullptr;
	return *this;
}

std::pair<Engine::MaterialParameterSet::const_iterator, bool>
Engine::MaterialParameterSet::try_emplace(const std::string& name, MaterialParameterValue value) {

	const MaterialParameterID id = MaterialParameterID::FromName(name);
	if (MaterialParameterRecord* record = FindRecord(id, name)) {
		return { const_iterator(record), false };
	}

	Data& data = EnsureData();
	const auto position = std::lower_bound(data.records.begin(), data.records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t value) {
			return record.id.value < value;
		});
	// ID順の位置へ新しい値を登録する
	const auto inserted = data.records.insert(position, MaterialParameterRecord{
		.id = id,
		.semantic = ResolveMaterialParameterSemantic(name),
		.namedValue = { name, std::move(value) },
		});
	Touch();
	return { const_iterator(&*inserted), true };
}

std::pair<Engine::MaterialParameterSet::const_iterator, bool>
Engine::MaterialParameterSet::emplace(const std::string& name, MaterialParameterValue value) {

	return try_emplace(name, std::move(value));
}

void Engine::MaterialParameterSet::clear() {

	// 設定値の領域を解除する
	data_.reset();
}

size_t Engine::MaterialParameterSet::erase(const std::string& name) {

	const_iterator position = find(name);
	if (position == end()) {
		return 0;
	}
	erase(position);
	return 1;
}

size_t Engine::MaterialParameterSet::erase(MaterialParameterID id) {

	if (!data_ || !id) {
		return 0;
	}

	const auto first = std::lower_bound(
		data_->records.begin(), data_->records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t target) {
			return record.id.value < target;
		});
	const auto last = std::upper_bound(
		first, data_->records.end(), id.value,
		[](uint64_t target, const MaterialParameterRecord& record) {
			return target < record.id.value;
		});
	const size_t count = static_cast<size_t>(std::distance(first, last));
	if (count == 0) {
		return 0;
	}

	// 同じIDの値をまとめて削除する
	data_->records.erase(first, last);
	Touch();
	if (data_->records.empty()) {
		data_.reset();
	}
	return count;
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::erase(const_iterator position) {

	if (!data_ || position.record_ == nullptr) {
		return end();
	}

	const ptrdiff_t index = position.record_ - data_->records.data();
	if (index < 0 || static_cast<size_t>(index) >= data_->records.size()) {
		return end();
	}

	// 削除した位置の次の値を返す
	const auto next = data_->records.erase(data_->records.begin() + index);
	Touch();
	if (data_->records.empty()) {
		data_.reset();
		return end();
	}
	return const_iterator(next == data_->records.end() ? data_->records.data() + data_->records.size() : &*next);
}

void Engine::MaterialParameterSet::Set(std::string_view name, const MaterialParameterValue& value) {

	const MaterialParameterID id = MaterialParameterID::FromName(name);
	if (MaterialParameterRecord* record = FindRecord(id, name)) {

		// 値を更新して内容Hashを失効する
		record->namedValue.second = value;
		Touch();
		return;
	}
	try_emplace(std::string(name), value);
}

void Engine::MaterialParameterSet::Set(
	MaterialParameterID id, std::string_view name,
	MaterialParameterSemantic semantic, const MaterialParameterValue& value) {

	if (!id) {
		id = MaterialParameterID::FromName(name);
	}
	if (MaterialParameterRecord* record = FindRecord(id)) {

		// 安定IDを維持して名前と用途と値を更新する
		record->namedValue.first = name;
		record->semantic = semantic;
		record->namedValue.second = value;
		Touch();
		return;
	}

	Data& data = EnsureData();
	const auto position = std::lower_bound(data.records.begin(), data.records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t target) {
			return record.id.value < target;
		});
	// 未登録のIDを検索順に追加する
	data.records.insert(position, MaterialParameterRecord{
		.id = id,
		.semantic = semantic,
		.namedValue = { std::string(name), value },
		});
	Touch();
}

void Engine::MaterialParameterSet::MergeFrom(const MaterialParameterSet& overrides) {

	// 同名の既存IDを優先して上書きを重ねる
	for (const MaterialParameterRecord& parameter : overrides.GetRecords()) {
		MaterialParameterID targetID = parameter.id;
		MaterialParameterSemantic targetSemantic = parameter.semantic;
		if (!FindRecord(targetID) && data_) {
			const auto sameName = std::find_if(
				data_->records.begin(), data_->records.end(),
				[&parameter](const MaterialParameterRecord& current) {

					return current.namedValue.first == parameter.namedValue.first;
				});
			if (sameName != data_->records.end()) {
				targetID = sameName->id;
				if (targetSemantic == MaterialParameterSemantic::None) {
					targetSemantic = sameName->semantic;
				}
			}
		}
		Set(targetID, parameter.namedValue.first,
			targetSemantic, parameter.namedValue.second);
	}
}

const Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(MaterialParameterSemantic semantic) const {

	if (!data_) {
		return nullptr;
	}
	for (const MaterialParameterRecord& record : data_->records) {
		if (record.semantic == semantic) {
			return &record.namedValue.second;
		}
	}
	return nullptr;
}

const Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(MaterialParameterID id) const {

	const MaterialParameterRecord* record = FindRecord(id);
	return record ? &record->namedValue.second : nullptr;
}

const Engine::MaterialParameterValue* Engine::MaterialParameterSet::FindByName(std::string_view name) const {

	if (!data_) {
		return nullptr;
	}
	for (const MaterialParameterRecord& record : data_->records) {

		if (record.namedValue.first == name) {
			return &record.namedValue.second;
		}
	}
	return nullptr;
}

size_t Engine::MaterialParameterSet::count(const std::string& name) const {

	return contains(name) ? 1 : 0;
}

bool Engine::MaterialParameterSet::contains(const std::string& name) const {

	return FindRecord(MaterialParameterID::FromName(name), name) != nullptr;
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::begin() const {

	return data_ && !data_->records.empty() ? const_iterator(data_->records.data()) : const_iterator{};
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::end() const {

	return data_ && !data_->records.empty() ?
		const_iterator(data_->records.data() + data_->records.size()) : const_iterator{};
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::find(const std::string& name) const {

	const MaterialParameterRecord* record =
		FindRecord(MaterialParameterID::FromName(name), name);
	return record ? const_iterator(record) : end();
}

std::span<const Engine::MaterialParameterRecord> Engine::MaterialParameterSet::GetRecords() const {

	return data_ ? std::span<const MaterialParameterRecord>(data_->records) :
		std::span<const MaterialParameterRecord>{};
}

uint64_t Engine::MaterialParameterSet::GetContentHash() const {

	if (!data_) {
		return 0;
	}
	// 値の変更がなければ計算済みのHashを返す
	if (!data_->contentHashDirty) {
		return data_->contentHash;
	}

	// IDと用途と名前と値を順番に混ぜる
	uint64_t hash = static_cast<uint64_t>(data_->records.size());
	for (const MaterialParameterRecord& record : data_->records) {
		hash = Algorithm::MixHash(hash, record.id.value);
		hash = Algorithm::MixHash(hash, static_cast<uint64_t>(record.semantic));
		hash = Algorithm::MixHashString(hash, record.namedValue.first);
		hash = Algorithm::MixHash(hash, MaterialParameterHash::HashValue(record.namedValue.second));
	}
	data_->contentHash = hash;
	data_->contentHashDirty = false;
	return hash;
}

Engine::MaterialParameterSet::Data& Engine::MaterialParameterSet::EnsureData() {

	if (!data_) {
		data_ = std::make_unique<Data>();
	}
	return *data_;
}

void Engine::MaterialParameterSet::Touch() {

	// 次の取得で内容Hashを再計算する
	++data_->revision;
	data_->contentHashDirty = true;
}

Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(MaterialParameterID id) {

	return data_ && id ? FindParameterRecord(data_->records, id) : nullptr;
}

const Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(MaterialParameterID id) const {

	return data_ && id ? FindParameterRecord(std::as_const(data_->records), id) : nullptr;
}

Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id, std::string_view name) {

	return data_ ? FindParameterRecord(data_->records, id, name) : nullptr;
}

const Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id, std::string_view name) const {

	return data_ ? FindParameterRecord(std::as_const(data_->records), id, name) : nullptr;
}
