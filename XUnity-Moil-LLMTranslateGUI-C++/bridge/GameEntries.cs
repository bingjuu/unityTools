using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using SimpleJSON;

namespace UnityTools
{
    public sealed class GameEntries
    {
        private sealed class Provider
        {
            public WeakReference target;
            public MethodInfo method;
            public string objectId;
            public string source;
        }
        private readonly Dictionary<string, Provider> providers = new Dictionary<string, Provider>();
        private readonly Dictionary<string, MethodInfo> consoles = new Dictionary<string, MethodInfo>();
        public Func<object, bool> IsAlive = value => value != null;
        public Func<string, object> ResolveObject;
        public Func<object, object, bool> SameObject = ReferenceEquals;
        public Func<object, string> RegisterObject;

        public void Clear() { providers.Clear(); consoles.Clear(); }
        public JSONArray DiscoverProviders(IEnumerable<object> objects)
        {
            foreach (var value in objects) RegisterProvider(value, null);
            return ProviderRows();
        }
        public void RegisterProvider(object value, string objectId)
        {
            if (value == null || !IsAlive(value) || value is Type) return;
            var type = value.GetType();
            MethodInfo method;
            try { method = type.GetMethod("GetCurrentPlayData", BindingFlags.Instance | BindingFlags.Public, null, Type.EmptyTypes, null); }
            catch { return; }
            if (method == null || method.ContainsGenericParameters || !DictionaryType(method.ReturnType)) return;
            foreach (var pair in providers)
                if (SameObject(pair.Value.target.Target, value)) {
                    if (!String.IsNullOrEmpty(objectId)) { pair.Value.target = new WeakReference(value); pair.Value.objectId = objectId; }
                    return;
                }
            var id = Guid.NewGuid().ToString("N");
            var record = new Provider { target = new WeakReference(value), method = method, objectId = objectId, source = type.AssemblyQualifiedName };
            providers[id] = record;
            if (String.IsNullOrEmpty(objectId) && RegisterObject != null)
            {
                var bound = RegisterObject(value);
                if (String.IsNullOrEmpty(record.objectId)) record.objectId = bound;
            }
        }
        private static bool DictionaryType(Type type)
        {
            return typeof(IDictionary).IsAssignableFrom(type) || (type.FullName ?? "").StartsWith("Il2CppSystem.Collections.Generic.Dictionary`", StringComparison.Ordinal);
        }
        public JSONArray ProviderRows()
        {
            var rows = new JSONArray();
            foreach (var pair in providers.ToArray())
            {
                var value = pair.Value.target.Target;
                if (value == null || !IsAlive(value) || !String.IsNullOrEmpty(pair.Value.objectId) && (ResolveObject == null || !SameObject(ResolveObject(pair.Value.objectId), value))) { providers.Remove(pair.Key); continue; }
                rows.Add(new JSONObject { ["providerId"] = pair.Key, ["type"] = pair.Value.source, ["source"] = pair.Value.objectId ?? "loaded-scene-object",
                    ["method"] = "GetCurrentPlayData", ["snapshotOnly"] = true });
            }
            return rows;
        }
        public JSONArray DiscoverConsoles(IEnumerable<Assembly> assemblies)
        {
            foreach (var assembly in assemblies)
            {
                var type = assembly.GetType("IngameDebugConsole.DebugLogConsole", false);
                if (type == null) continue;
                var method = type.GetMethod("ExecuteCommand", BindingFlags.Public | BindingFlags.Static, null, new[] { typeof(string) }, null);
                if (method == null || method.ReturnType != typeof(void) || method.ContainsGenericParameters || method.DeclaringType != type) continue;
                if (consoles.Values.Contains(method)) continue;
                consoles[Guid.NewGuid().ToString("N")] = method;
            }
            var rows = new JSONArray();
            foreach (var pair in consoles) rows.Add(new JSONObject { ["consoleId"] = pair.Key, ["type"] = pair.Value.DeclaringType.AssemblyQualifiedName,
                ["source"] = "UnityIngameDebugConsole.ExecuteCommand(string)", ["resultScope"] = "submitted" });
            return rows;
        }
        public JSONNode ReadPlayData(JSONNode args)
        {
            var started = false;
            try
            {
                Provider provider;
                var id = args["providerId"].Value;
                if (String.IsNullOrEmpty(id) || !providers.TryGetValue(id, out provider)) return Error("provider-selection-required", false);
                var target = provider.target.Target;
                if (target == null || !IsAlive(target)) return Error("stale-reference", false);
                if (!String.IsNullOrEmpty(provider.objectId) && (ResolveObject == null || !SameObject(ResolveObject(provider.objectId), target))) return Error("stale-reference", false);
                started = true;
                var dictionary = provider.method.Invoke(target, null);
                var rows = new JSONArray();
                var managed = dictionary as IDictionary;
                if (managed != null)
                {
                    foreach (DictionaryEntry entry in managed) rows.Add(Row(entry.Key, entry.Value));
                }
                else if (dictionary != null)
                {
                    var type = dictionary.GetType();
                    var keys = type.GetProperty("Keys").GetValue(dictionary, null);
                    var enumerator = keys.GetType().GetMethod("GetEnumerator", Type.EmptyTypes).Invoke(keys, null);
                    var move = enumerator.GetType().GetMethod("MoveNext", Type.EmptyTypes);
                    var current = enumerator.GetType().GetProperty("Current");
                    var indexer = type.GetProperties().First(p => p.Name == "Item" && p.GetIndexParameters().Length == 1);
                    while ((bool)move.Invoke(enumerator, null))
                    {
                        var key = current.GetValue(enumerator, null);
                        rows.Add(Row(key, indexer.GetValue(dictionary, new[] { key })));
                    }
                }
                else return Error("playdata-is-not-dictionary", true);
                return new JSONObject { ["rows"] = rows, ["providerId"] = id, ["snapshotOnly"] = true, ["state"] = "completed" };
            }
            catch (Exception error) { return Error((error.InnerException ?? error).Message, started); }
        }
        private static JSONNode Row(object key, object value)
        {
            var type = value == null ? null : value.GetType();
            var text = type == null ? "" : type.IsPrimitive || type.IsEnum || value is string || value is decimal
                ? Convert.ToString(value, CultureInfo.InvariantCulture) : type.FullName;
            return new JSONObject { ["key"] = Convert.ToString(key, CultureInfo.InvariantCulture), ["value"] = text,
                ["type"] = type == null ? "null" : type.AssemblyQualifiedName };
        }
        public JSONNode SendCommand(JSONNode args)
        {
            MethodInfo method;
            if (String.IsNullOrEmpty(args["consoleId"].Value) || !consoles.TryGetValue(args["consoleId"].Value, out method)) return Error("identified-console-required", false);
            if (!args["cmd"].IsString) return Error("command-text-required", false);
            try
            {
                method.Invoke(null, new object[] { args["cmd"].Value });
                return new JSONObject { ["state"] = "submitted", ["reason"] = "registered-game-command-entry-called", ["consoleId"] = args["consoleId"].Value };
            }
            catch (Exception error) { return Error((error.InnerException ?? error).Message, true); }
        }
        private static JSONNode Error(string error, bool started)
        {
            return new JSONObject { ["state"] = started ? "executed-unverified" : "not-executed", ["error"] = error };
        }
    }
}
