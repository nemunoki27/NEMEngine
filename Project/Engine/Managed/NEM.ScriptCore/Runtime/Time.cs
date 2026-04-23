namespace NEMEngine;

public static class Time {

    public static float deltaTime => NativeApi.ReadDeltaTime();
}
