namespace NEMEngine;

// 保存前と復元後に保存用Fieldを整える
public interface ISerializationCallbackReceiver {
    void OnBeforeSerialize();
    void OnAfterDeserialize();
}
