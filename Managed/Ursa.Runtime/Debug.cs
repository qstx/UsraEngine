namespace Ursa;

public static class Debug
{
	public static void Log(string message) => EngineApi.WriteLog(2, message);

	public static void LogWarning(string message) => EngineApi.WriteLog(3, message);

	public static void LogError(string message) => EngineApi.WriteLog(4, message);
}
