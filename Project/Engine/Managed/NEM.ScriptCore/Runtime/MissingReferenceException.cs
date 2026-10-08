namespace NEMEngine;

// Native個体が失効した参照への操作を通知する
public class MissingReferenceException : SystemException {

    public MissingReferenceException() { }
    public MissingReferenceException(string message) : base(message) { }
    public MissingReferenceException(string message, Exception innerException) : base(message, innerException) { }
}
