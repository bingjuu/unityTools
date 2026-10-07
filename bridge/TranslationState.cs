using System;
using System.Reflection;
using SimpleJSON;

namespace UnityTools
{
    internal static class TranslationState
    {
        public static JSONObject Read()
        {
            var result = new JSONObject { ["scope"] = "next-launch", ["reason"] = "XUAT 5.6.2 does not provide verified reversible live request/display control" };
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (assembly.GetName().Name != "XUnity.AutoTranslator.Plugin.Core") continue;
                result["deployed"] = true;
                var settings = assembly.GetType("XUnity.AutoTranslator.Plugin.Core.Configuration.Settings", false);
                if (settings == null) { result["state"] = "unknown"; return result; }
                var endpoint = settings.GetField("ServiceEndpoint", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
                var shutdown = settings.GetField("IsShutdown", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
                var id = endpoint == null ? null : endpoint.GetValue(null) as string;
                result["endpoint"] = id ?? "";
                if (id == "Passthrough" || shutdown != null && (bool)shutdown.GetValue(null)) result["enabled"] = false;
                else if (id == "UnityToolsTranslate") result["enabled"] = true;
                else result["state"] = "unknown";
                return result;
            }
            result["deployed"] = false;
            result["state"] = "unknown";
            return result;
        }
        public static JSONObject Set(JSONNode args)
        {
            var result = Read();
            result["state"] = "not-executed";
            result["error"] = "live-translation-control-unavailable; set UT preference for next launch";
            if (args["enabled"].IsBoolean) result["requested"] = args["enabled"];
            return result;
        }
    }
}
