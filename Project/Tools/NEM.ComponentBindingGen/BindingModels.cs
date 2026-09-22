namespace NEM.ComponentBindingGen;

internal sealed class EnumMember { public string Name = ""; public long Value; }
internal sealed class EnumModel {
        public string ManagedType = "";
        public string NativeType = "";
        public List<EnumMember> Members = new();
    }
internal sealed class PropertyModel {
        public string ManagedName = "";
        public string NativeMember = "";
        public string Kind = "";
        public string? AssetType;
        public string? EnumType;
        public string Access = "ReadWrite";
        public bool ReadOnly => string.Equals(Access, "ReadOnly", StringComparison.Ordinal);
        // C# property のアクセス修飾子、形状ごとのネストアクセサへ委譲する低レベル property は Internal にする
        public string Visibility = "Public";
        public string CsVisibility => string.Equals(Visibility, "Internal", StringComparison.Ordinal) ? "internal" : "public";
    }
internal sealed class ComponentModel {
        public int ID = -1;
        public string RegistryName = "";
        public string NativeType = "";
        public string NativeHeader = "";
        public string Exposure = "";
        public string ManagedType = "";
        public bool ManagedFactory;
        public bool AllowAdd = true;
        public bool AllowRemove = true;
        public List<PropertyModel> Properties = new();
    }
internal sealed class AbiFieldModel {
        public string Name = "";
        public string NativeType = "";
        public string ManagedType = "";
    }

