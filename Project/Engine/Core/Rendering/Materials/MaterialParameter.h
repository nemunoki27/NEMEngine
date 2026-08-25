#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Engine {

	//============================================================================
	//	MaterialParameter structures
	//============================================================================
	// 実行時の文字列検索を避ける安定パラメータID
	struct MaterialParameterID {

		uint64_t value = 0;

		bool operator==(const MaterialParameterID&) const noexcept = default;
		explicit operator bool() const noexcept { return value != 0; }

		// 既存HLSL変数名から決定的なIDを生成する
		static constexpr MaterialParameterID FromName(std::string_view name) noexcept {

			uint64_t hash = 14695981039346656037ull;
			for (const char character : name) {
				hash ^= static_cast<uint8_t>(character);
				hash *= 1099511628211ull;
			}
			return MaterialParameterID{ hash != 0 ? hash : 1 };
		}

		// ShaderGraphの公開パラメータUUIDをそのままIDとして使う
		static constexpr MaterialParameterID FromUUID(UUID uuid) noexcept {

			return MaterialParameterID{ uuid.value };
		}
	};

	// 標準マテリアルが持つ意味
	enum class MaterialParameterSemantic : uint16_t {

		None = 0,
		BaseColor,
		BaseColorTexture,
		NormalTexture,
		Metallic,
		MetallicTexture,
		Roughness,
		RoughnessTexture,
		DisplacementTexture,
		DisplacementScale,
		DisplacementMidpoint,
		AmbientOcclusion,
		AmbientOcclusionTexture,
		EmissiveColor,
		EmissiveTexture,
		Opacity,
		AlphaClip,
		UVTransform,
		MetallicRoughnessTexture,
		EmissiveIntensity,
	};

	// 標準PBRパラメータ名
	namespace MaterialParameterNames {

		inline constexpr std::string_view BaseColor = "color";
		inline constexpr std::string_view BaseColorTexture = "baseColorTexture";
		inline constexpr std::string_view NormalTexture = "normalTexture";
		inline constexpr std::string_view Metallic = "metallic";
		inline constexpr std::string_view MetallicRoughnessTexture = "metallicRoughnessTexture";
		inline constexpr std::string_view MetallicTexture = "metallicTexture";
		inline constexpr std::string_view Roughness = "roughness";
		inline constexpr std::string_view RoughnessTexture = "roughnessTexture";
		inline constexpr std::string_view DisplacementTexture = "displacementTexture";
		inline constexpr std::string_view DisplacementScale = "displacementScale";
		inline constexpr std::string_view DisplacementMidpoint = "displacementMidpoint";
		inline constexpr std::string_view AmbientOcclusion = "ambientOcclusion";
		inline constexpr std::string_view AmbientOcclusionTexture = "occlusionTexture";
		inline constexpr std::string_view EmissiveColor = "emissiveColor";
		inline constexpr std::string_view EmissiveTexture = "emissiveTexture";
		inline constexpr std::string_view SpecularTexture = "specularTexture";
		inline constexpr std::string_view EmissiveIntensity = "emissiveIntensity";
		inline constexpr std::string_view Opacity = "opacity";
		inline constexpr std::string_view AlphaClip = "alphaClip";
		inline constexpr std::string_view SelectionMode = "selectionMode";
		inline constexpr std::string_view CompositeMode = "compositeMode";
		inline constexpr std::string_view RenderingLayerMask =
			"renderingLayerMask";
	}

	// 標準PBRパラメータID
	namespace MaterialParameterIDs {

		inline constexpr MaterialParameterID SelectionMode =
			MaterialParameterID::FromName(MaterialParameterNames::SelectionMode);
		inline constexpr MaterialParameterID CompositeMode =
			MaterialParameterID::FromName(MaterialParameterNames::CompositeMode);
		inline constexpr MaterialParameterID RenderingLayerMask =
			MaterialParameterID::FromName(
				MaterialParameterNames::RenderingLayerMask);

		inline constexpr MaterialParameterID BaseColor =
			MaterialParameterID::FromName(MaterialParameterNames::BaseColor);
		inline constexpr MaterialParameterID BaseColorTexture =
			MaterialParameterID::FromName(MaterialParameterNames::BaseColorTexture);
		inline constexpr MaterialParameterID NormalTexture =
			MaterialParameterID::FromName(MaterialParameterNames::NormalTexture);
		inline constexpr MaterialParameterID Metallic =
			MaterialParameterID::FromName(MaterialParameterNames::Metallic);
		inline constexpr MaterialParameterID MetallicRoughnessTexture =
			MaterialParameterID::FromName(MaterialParameterNames::MetallicRoughnessTexture);
		inline constexpr MaterialParameterID MetallicTexture =
			MaterialParameterID::FromName(MaterialParameterNames::MetallicTexture);
		inline constexpr MaterialParameterID Roughness =
			MaterialParameterID::FromName(MaterialParameterNames::Roughness);
		inline constexpr MaterialParameterID RoughnessTexture =
			MaterialParameterID::FromName(MaterialParameterNames::RoughnessTexture);
		inline constexpr MaterialParameterID DisplacementTexture =
			MaterialParameterID::FromName(MaterialParameterNames::DisplacementTexture);
		inline constexpr MaterialParameterID DisplacementScale =
			MaterialParameterID::FromName(MaterialParameterNames::DisplacementScale);
		inline constexpr MaterialParameterID DisplacementMidpoint =
			MaterialParameterID::FromName(MaterialParameterNames::DisplacementMidpoint);
		inline constexpr MaterialParameterID AmbientOcclusion =
			MaterialParameterID::FromName(MaterialParameterNames::AmbientOcclusion);
		inline constexpr MaterialParameterID AmbientOcclusionTexture =
			MaterialParameterID::FromName(MaterialParameterNames::AmbientOcclusionTexture);
		inline constexpr MaterialParameterID EmissiveColor =
			MaterialParameterID::FromName(MaterialParameterNames::EmissiveColor);
		inline constexpr MaterialParameterID EmissiveTexture =
			MaterialParameterID::FromName(MaterialParameterNames::EmissiveTexture);
		inline constexpr MaterialParameterID SpecularTexture =
			MaterialParameterID::FromName(MaterialParameterNames::SpecularTexture);
		inline constexpr MaterialParameterID EmissiveIntensity =
			MaterialParameterID::FromName(MaterialParameterNames::EmissiveIntensity);
		inline constexpr MaterialParameterID Opacity =
			MaterialParameterID::FromName(MaterialParameterNames::Opacity);
		inline constexpr MaterialParameterID AlphaClip =
			MaterialParameterID::FromName(MaterialParameterNames::AlphaClip);
	}

	// マテリアルのパラメータ値
	struct MaterialParameterValue {

		std::variant<float, Vector2, Vector3, Vector4, Color4, AssetID, int32_t, uint32_t, bool> value;
	};
	// 型を増減したらpackとJSON変換を更新すること
	static_assert(std::variant_size_v<decltype(MaterialParameterValue::value)> == 9);

	// ID順で保持する1パラメータ
	struct MaterialParameterRecord {

		MaterialParameterID id{};
		MaterialParameterSemantic semantic = MaterialParameterSemantic::None;
		std::pair<std::string, MaterialParameterValue> namedValue{};
	};

	//============================================================================
	//	MaterialParameterSet class
	//	ID順の連続領域へ疎なマテリアルパラメータを保持する
	//============================================================================
	class MaterialParameterSet {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		using value_type = std::pair<std::string, MaterialParameterValue>;

		class const_iterator;
		class iterator {
		public:
			iterator() = default;

			value_type& operator*() const { return record_->namedValue; }
			value_type* operator->() const { return &record_->namedValue; }
			iterator& operator++() { ++record_; return *this; }
			iterator operator++(int) { const iterator current = *this; ++record_; return current; }
			bool operator==(const iterator&) const noexcept = default;
		private:
			friend class MaterialParameterSet;
			friend class const_iterator;
			explicit iterator(MaterialParameterRecord* record) : record_(record) {}

			MaterialParameterRecord* record_ = nullptr;
		};

		class const_iterator {
		public:
			const_iterator() = default;
			const_iterator(iterator other) : record_(other.record_) {}

			const value_type& operator*() const { return record_->namedValue; }
			const value_type* operator->() const { return &record_->namedValue; }
			const_iterator& operator++() { ++record_; return *this; }
			const_iterator operator++(int) { const const_iterator current = *this; ++record_; return current; }
			bool operator==(const const_iterator&) const noexcept = default;
		private:
			friend class MaterialParameterSet;
			explicit const_iterator(const MaterialParameterRecord* record) : record_(record) {}

			const MaterialParameterRecord* record_ = nullptr;
		};

		MaterialParameterSet() = default;
		~MaterialParameterSet() = default;
		MaterialParameterSet(const MaterialParameterSet& other);
		MaterialParameterSet(MaterialParameterSet&& other) noexcept = default;
		MaterialParameterSet& operator=(const MaterialParameterSet& other);
		MaterialParameterSet& operator=(MaterialParameterSet&& other) noexcept = default;

		MaterialParameterValue& operator[](const std::string& name);
		MaterialParameterValue& operator[](const char* name);
		std::pair<iterator, bool> try_emplace(const std::string& name, MaterialParameterValue value);
		std::pair<iterator, bool> emplace(const std::string& name, MaterialParameterValue value);
		void clear();
		size_t erase(const std::string& name);
		size_t erase(MaterialParameterID id);
		iterator erase(iterator position);

		// IDとSemanticを明示してShaderGraph公開パラメータを設定する
		void Set(MaterialParameterID id, std::string_view name,
			MaterialParameterSemantic semantic, const MaterialParameterValue& value);
		// 標準Semanticから設定済みパラメータを検索する
		MaterialParameterValue* Find(MaterialParameterSemantic semantic);
		const MaterialParameterValue* Find(MaterialParameterSemantic semantic) const;
		// 安定IDから設定済みパラメータを検索する
		MaterialParameterValue* Find(MaterialParameterID id);
		const MaterialParameterValue* Find(MaterialParameterID id) const;
		// Shader Graphの表示名からUUID付きパラメータを解決する低頻度フォールバック
		MaterialParameterValue* FindByName(std::string_view name);
		const MaterialParameterValue* FindByName(std::string_view name) const;

		//--------- accessor -----------------------------------------------------

		bool empty() const { return !data_ || data_->records.empty(); }
		size_t size() const { return data_ ? data_->records.size() : 0; }
		size_t count(const std::string& name) const;
		bool contains(const std::string& name) const;
		iterator begin();
		iterator end();
		const_iterator begin() const;
		const_iterator end() const;
		iterator find(const std::string& name);
		const_iterator find(const std::string& name) const;
		std::span<const MaterialParameterRecord> GetRecords() const;
		uint64_t GetRevision() const { return data_ ? data_->revision : 0; }
		uint64_t GetContentHash() const;
		const MaterialParameterSet& Get() const { return *this; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct Data {

			std::vector<MaterialParameterRecord> records{};
			uint64_t revision = 1;
			mutable uint64_t contentHash = 0;
			mutable bool contentHashDirty = true;
		};

		//--------- functions ----------------------------------------------------

		Data& EnsureData();
		void Touch();
		MaterialParameterRecord* FindRecord(MaterialParameterID id);
		const MaterialParameterRecord* FindRecord(MaterialParameterID id) const;
		MaterialParameterRecord* FindRecord(MaterialParameterID id, std::string_view name);
		const MaterialParameterRecord* FindRecord(MaterialParameterID id, std::string_view name) const;

		//--------- variables ----------------------------------------------------

		std::unique_ptr<Data> data_{};
	};

	// 既存HLSL名を標準Semanticへ解決する
	MaterialParameterSemantic ResolveMaterialParameterSemantic(std::string_view name);
	// テクスチャをsRGBとして読むSemanticか判定する
	bool IsSRGBMaterialTexture(MaterialParameterSemantic semantic);
} // Engine

//============================================================================
//	std::hash
//============================================================================
namespace std {

	template<>
	struct hash<Engine::MaterialParameterID> {

		size_t operator()(const Engine::MaterialParameterID& id) const noexcept {
			return std::hash<uint64_t>{}(id.value);
		}
	};
}
