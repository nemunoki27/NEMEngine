#include "MaterialParameter.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <type_traits>

namespace {

	struct SemanticAlias {

		std::string_view name;
		Engine::MaterialParameterSemantic semantic = Engine::MaterialParameterSemantic::None;
	};

	constexpr std::array kSemanticAliases = {
		SemanticAlias{ "color", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "basecolor", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "albedo", Engine::MaterialParameterSemantic::BaseColor },
		SemanticAlias{ "maintexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "basecolortexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "albedotexture", Engine::MaterialParameterSemantic::BaseColorTexture },
		SemanticAlias{ "normaltexture", Engine::MaterialParameterSemantic::NormalTexture },
		SemanticAlias{ "metallic", Engine::MaterialParameterSemantic::Metallic },
		SemanticAlias{ "metallicroughnesstexture", Engine::MaterialParameterSemantic::MetallicRoughnessTexture },
		SemanticAlias{ "metallictexture", Engine::MaterialParameterSemantic::MetallicTexture },
		SemanticAlias{ "roughness", Engine::MaterialParameterSemantic::Roughness },
		SemanticAlias{ "roughnesstexture", Engine::MaterialParameterSemantic::RoughnessTexture },
		SemanticAlias{ "displacementtexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "disptexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "heighttexture", Engine::MaterialParameterSemantic::DisplacementTexture },
		SemanticAlias{ "displacementscale", Engine::MaterialParameterSemantic::DisplacementScale },
		SemanticAlias{ "displacementmidpoint", Engine::MaterialParameterSemantic::DisplacementMidpoint },
		SemanticAlias{ "ambientocclusion", Engine::MaterialParameterSemantic::AmbientOcclusion },
		SemanticAlias{ "ao", Engine::MaterialParameterSemantic::AmbientOcclusion },
		SemanticAlias{ "ambientocclusiontexture", Engine::MaterialParameterSemantic::AmbientOcclusionTexture },
		SemanticAlias{ "aotexture", Engine::MaterialParameterSemantic::AmbientOcclusionTexture },
		SemanticAlias{ "emissivecolor", Engine::MaterialParameterSemantic::EmissiveColor },
		SemanticAlias{ "emissioncolor", Engine::MaterialParameterSemantic::EmissiveColor },
		SemanticAlias{ "emissivetexture", Engine::MaterialParameterSemantic::EmissiveTexture },
		SemanticAlias{ "emissiontexture", Engine::MaterialParameterSemantic::EmissiveTexture },
		SemanticAlias{ "emissiveintensity", Engine::MaterialParameterSemantic::EmissiveIntensity },
		SemanticAlias{ "opacity", Engine::MaterialParameterSemantic::Opacity },
		SemanticAlias{ "alphaclip", Engine::MaterialParameterSemantic::AlphaClip },
		SemanticAlias{ "alphacutoff", Engine::MaterialParameterSemantic::AlphaClip },
		SemanticAlias{ "uvtransform", Engine::MaterialParameterSemantic::UVTransform },
	};

	std::string NormalizeParameterName(std::string_view name) {

		std::string normalized;
		normalized.reserve(name.size());
		for (const char character : name) {
			if (character == '_' || character == '-' || character == ' ') {
				continue;
			}
			normalized.push_back(static_cast<char>(
				std::tolower(static_cast<unsigned char>(character))));
		}
		return normalized;
	}

	uint64_t HashCombine(uint64_t seed, uint64_t value) {

		return seed ^ (value + 0x9e3779b97f4a7c15ull +
			(seed << 6) + (seed >> 2));
	}

	uint64_t HashParameterValue(const Engine::MaterialParameterValue& parameter) {

		uint64_t hash = parameter.value.index();
		const auto appendFloat = [&](float value) {
			hash = HashCombine(hash, std::bit_cast<uint32_t>(value));
			};
		std::visit([&](const auto& value) {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				appendFloat(value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				appendFloat(value.x);
				appendFloat(value.y);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
				appendFloat(value.w);
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				appendFloat(value.r);
				appendFloat(value.g);
				appendFloat(value.b);
				appendFloat(value.a);
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				hash = HashCombine(hash, value.high);
				hash = HashCombine(hash, value.low);
			} else {
				hash = HashCombine(hash, static_cast<uint64_t>(value));
			}
			}, parameter.value);
		return hash;
	}

	uint64_t HashString(uint64_t seed, std::string_view value) {

		for (const char character : value) {
			seed = HashCombine(
				seed,
				static_cast<uint8_t>(character));
		}
		return seed;
	}
}

//============================================================================
//	MaterialParameterSet classMethods
//============================================================================
Engine::MaterialParameterSet::MaterialParameterSet(const MaterialParameterSet& other) {

	if (other.data_) {
		data_ = std::make_unique<Data>(*other.data_);
	}
}

Engine::MaterialParameterSet& Engine::MaterialParameterSet::operator=(
	const MaterialParameterSet& other) {

	if (this == &other) {
		return *this;
	}
	data_ = other.data_ ? std::make_unique<Data>(*other.data_) : nullptr;
	return *this;
}

Engine::MaterialParameterValue& Engine::MaterialParameterSet::operator[](
	const std::string& name) {

	const MaterialParameterID id = MaterialParameterID::FromName(name);
	if (MaterialParameterRecord* record = FindRecord(id, name)) {
		Touch();
		return record->namedValue.second;
	}

	Data& data = EnsureData();
	const auto position = std::lower_bound(data.records.begin(), data.records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t value) {
			return record.id.value < value;
		});
	const auto inserted = data.records.insert(position, MaterialParameterRecord{
		.id = id,
		.semantic = ResolveMaterialParameterSemantic(name),
		.namedValue = { name, MaterialParameterValue{} },
		});
	++data.revision;
	data.contentHashDirty = true;
	return inserted->namedValue.second;
}

Engine::MaterialParameterValue& Engine::MaterialParameterSet::operator[](const char* name) {

	return operator[](std::string(name));
}

std::pair<Engine::MaterialParameterSet::iterator, bool>
Engine::MaterialParameterSet::try_emplace(
	const std::string& name, MaterialParameterValue value) {

	const MaterialParameterID id = MaterialParameterID::FromName(name);
	if (MaterialParameterRecord* record = FindRecord(id, name)) {
		return { iterator(record), false };
	}

	Data& data = EnsureData();
	const auto position = std::lower_bound(data.records.begin(), data.records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t value) {
			return record.id.value < value;
		});
	const auto inserted = data.records.insert(position, MaterialParameterRecord{
		.id = id,
		.semantic = ResolveMaterialParameterSemantic(name),
		.namedValue = { name, std::move(value) },
		});
	++data.revision;
	data.contentHashDirty = true;
	return { iterator(&*inserted), true };
}

std::pair<Engine::MaterialParameterSet::iterator, bool>
Engine::MaterialParameterSet::emplace(
	const std::string& name, MaterialParameterValue value) {

	return try_emplace(name, std::move(value));
}

void Engine::MaterialParameterSet::clear() {

	data_.reset();
}

size_t Engine::MaterialParameterSet::erase(const std::string& name) {

	iterator position = find(name);
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

	data_->records.erase(first, last);
	++data_->revision;
	data_->contentHashDirty = true;
	if (data_->records.empty()) {
		data_.reset();
	}
	return count;
}

Engine::MaterialParameterSet::iterator Engine::MaterialParameterSet::erase(iterator position) {

	if (!data_ || position.record_ == nullptr) {
		return end();
	}

	const ptrdiff_t index = position.record_ - data_->records.data();
	if (index < 0 || static_cast<size_t>(index) >= data_->records.size()) {
		return end();
	}

	const auto next = data_->records.erase(data_->records.begin() + index);
	++data_->revision;
	data_->contentHashDirty = true;
	if (data_->records.empty()) {
		data_.reset();
		return end();
	}
	return iterator(next == data_->records.end() ? data_->records.data() + data_->records.size() : &*next);
}

void Engine::MaterialParameterSet::Set(
	MaterialParameterID id, std::string_view name,
	MaterialParameterSemantic semantic, const MaterialParameterValue& value) {

	if (!id) {
		id = MaterialParameterID::FromName(name);
	}
	if (MaterialParameterRecord* record = FindRecord(id)) {
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
	data.records.insert(position, MaterialParameterRecord{
		.id = id,
		.semantic = semantic,
		.namedValue = { std::string(name), value },
		});
	++data.revision;
	data.contentHashDirty = true;
}

Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(
	MaterialParameterSemantic semantic) {

	if (!data_) {
		return nullptr;
	}
	for (MaterialParameterRecord& record : data_->records) {
		if (record.semantic == semantic) {
			Touch();
			return &record.namedValue.second;
		}
	}
	return nullptr;
}

const Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(
	MaterialParameterSemantic semantic) const {

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

Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(MaterialParameterID id) {

	MaterialParameterRecord* record = FindRecord(id);
	if (!record) {
		return nullptr;
	}
	Touch();
	return &record->namedValue.second;
}

const Engine::MaterialParameterValue* Engine::MaterialParameterSet::Find(
	MaterialParameterID id) const {

	const MaterialParameterRecord* record = FindRecord(id);
	return record ? &record->namedValue.second : nullptr;
}

Engine::MaterialParameterValue*
Engine::MaterialParameterSet::FindByName(
	std::string_view name) {

	MaterialParameterValue* value =
		const_cast<MaterialParameterValue*>(
		static_cast<const MaterialParameterSet*>(this)->
		FindByName(name));
	if (value) {
		Touch();
	}
	return value;
}

const Engine::MaterialParameterValue*
Engine::MaterialParameterSet::FindByName(
	std::string_view name) const {

	if (!data_) {
		return nullptr;
	}
	for (const MaterialParameterRecord& record :
		data_->records) {

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

Engine::MaterialParameterSet::iterator Engine::MaterialParameterSet::begin() {

	Touch();
	return data_ && !data_->records.empty() ? iterator(data_->records.data()) : iterator{};
}

Engine::MaterialParameterSet::iterator Engine::MaterialParameterSet::end() {

	return data_ && !data_->records.empty() ?
		iterator(data_->records.data() + data_->records.size()) : iterator{};
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::begin() const {

	return data_ && !data_->records.empty() ? const_iterator(data_->records.data()) : const_iterator{};
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::end() const {

	return data_ && !data_->records.empty() ?
		const_iterator(data_->records.data() + data_->records.size()) : const_iterator{};
}

Engine::MaterialParameterSet::iterator Engine::MaterialParameterSet::find(
	const std::string& name) {

	Touch();
	MaterialParameterRecord* record =
		FindRecord(MaterialParameterID::FromName(name), name);
	return record ? iterator(record) : end();
}

Engine::MaterialParameterSet::const_iterator Engine::MaterialParameterSet::find(
	const std::string& name) const {

	const MaterialParameterRecord* record =
		FindRecord(MaterialParameterID::FromName(name), name);
	return record ? const_iterator(record) : end();
}

std::span<const Engine::MaterialParameterRecord>
Engine::MaterialParameterSet::GetRecords() const {

	return data_ ? std::span<const MaterialParameterRecord>(data_->records) :
		std::span<const MaterialParameterRecord>{};
}

uint64_t Engine::MaterialParameterSet::GetContentHash() const {

	if (!data_) {
		return 0;
	}
	if (!data_->contentHashDirty) {
		return data_->contentHash;
	}

	uint64_t hash = static_cast<uint64_t>(data_->records.size());
	for (const MaterialParameterRecord& record : data_->records) {
		hash = HashCombine(hash, record.id.value);
		hash = HashCombine(hash,
			static_cast<uint64_t>(record.semantic));
		hash = HashString(hash, record.namedValue.first);
		hash = HashCombine(hash, HashParameterValue(record.namedValue.second));
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

	if (!data_) {
		return;
	}
	++data_->revision;
	data_->contentHashDirty = true;
}

Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id) {

	return const_cast<MaterialParameterRecord*>(
		static_cast<const MaterialParameterSet*>(this)->FindRecord(id));
}

const Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id) const {

	if (!data_ || !id) {
		return nullptr;
	}
	const auto position = std::lower_bound(
		data_->records.begin(), data_->records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t target) {
			return record.id.value < target;
		});
	return position != data_->records.end() && position->id == id ?
		&*position : nullptr;
}

Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id, std::string_view name) {

	return const_cast<MaterialParameterRecord*>(
		static_cast<const MaterialParameterSet*>(this)->FindRecord(id, name));
}

const Engine::MaterialParameterRecord* Engine::MaterialParameterSet::FindRecord(
	MaterialParameterID id, std::string_view name) const {

	if (!data_) {
		return nullptr;
	}
	auto position = std::lower_bound(data_->records.begin(), data_->records.end(), id.value,
		[](const MaterialParameterRecord& record, uint64_t target) {
			return record.id.value < target;
		});
	for (; position != data_->records.end() && position->id == id; ++position) {
		if (position->namedValue.first == name) {
			return &*position;
		}
	}
	return nullptr;
}

Engine::MaterialParameterSemantic Engine::ResolveMaterialParameterSemantic(
	std::string_view name) {

	const std::string normalized = NormalizeParameterName(name);
	for (const SemanticAlias& alias : kSemanticAliases) {
		if (alias.name == normalized) {
			return alias.semantic;
		}
	}
	return MaterialParameterSemantic::None;
}

bool Engine::IsSRGBMaterialTexture(MaterialParameterSemantic semantic) {

	return semantic == MaterialParameterSemantic::BaseColorTexture ||
		semantic == MaterialParameterSemantic::EmissiveTexture;
}
