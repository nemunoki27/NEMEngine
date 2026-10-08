#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	// JSONのkind文字列から解決する保存Field種別
	enum class ManagedSerializedFieldKind : int32_t {

		None = 0,
		Bool,
		Byte,
		SByte,
		Short,
		UShort,
		Int,
		UInt,
		Long,
		ULong,
		Float,
		Double,
		String,
		Enum,
		Vector2,
		Vector3,
		Vector4,
		Quaternion,
		Color3,
		Color4,
		Nullable,
		Array,
		List,
		AssetRef,
		EntityRef,
		ScriptRef,
		ComponentRef,
		Object,
		ManagedReference,
		Unsupported,
	};

	// Fieldの型と編集属性を保持する読取用データ
	struct ManagedFieldSchema {

		//--------- structure ----------------------------------------------------

		// 参照値の候補型
		struct ReferenceCandidate {

			std::string type; // 候補型の完全名
			std::vector<std::shared_ptr<const ManagedFieldSchema>> members;
		};

		//--------- variables ----------------------------------------------------

		std::string fieldID;	   // 保存FieldのGUID
		std::string name;		   // 現在のField名
		std::string declaringType; // 宣言型で継承時の識別に使う

		ManagedSerializedFieldKind kind = ManagedSerializedFieldKind::None;
		std::shared_ptr<const ManagedFieldSchema> element; // 配列とListとNullableの要素

		// 列挙値
		std::string enumUnderlying;
		std::vector<std::string> enumNames;
		std::vector<std::string> enumValues; // 整数の精度を保つ文字列表現

		// 参照先の型
		std::string assetType;	   // Assetの型名
		std::string scriptType;	   // Scriptの型完全名
		std::string componentType; // Componentの登録名

		// Objectと参照値の型完全名
		std::string objectType;
		// Objectのメンバ情報
		std::vector<std::shared_ptr<const ManagedFieldSchema>> members;
		// 参照値の候補一覧
		std::vector<ReferenceCandidate> candidates;

		// Inspector属性
		bool isPublic = false;
		bool isReadOnly = false;
		bool isHidden = false;
		bool multiline = false;
		bool hasRange = false;
		float rangeMin = 0.0f;
		float rangeMax = 0.0f;
		bool hasMin = false;
		float minValue = 0.0f;
		bool hasDragSpeed = false;
		float dragSpeed = 0.0f;
		std::string tooltip;
		std::string header; // 区切りの見出し
		std::string label;	// 表示名の上書き

		// 編集値が未設定の場合の既定値JSON
		std::string defaultValueJSON;
	};

	// Script型ごとの保存Field情報
	struct ManagedScriptSchema {

		std::string scriptTypeID;
		std::string fullTypeName;
		int32_t schemaVersion = 0;
		std::vector<ManagedFieldSchema> fields;
	};

} // Engine
