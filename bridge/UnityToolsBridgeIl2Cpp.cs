using System;
using System.Collections.Concurrent;
using System.IO;
using System.Collections.Generic;
using System.Text;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using SimpleJSON;
using UnityEngine;

namespace UnityTools
{
    [BepInPlugin("unitytools.bridge", "UnityToolsBridge", "1.0.0")]
    public sealed class UnityToolsBridgeIl2Cpp : BasePlugin
    {
        internal static readonly ConcurrentQueue<Action> Queue = new ConcurrentQueue<Action>();
        private static HttpListenerHost host;
        private static BridgeProtocol protocol;
        private static Il2CppObjectAccess access;
        private static GameEntries entries;
        private static string handshake;

        public override void Load()
        {
            var session = Environment.GetEnvironmentVariable("UNITYTOOLS_SESSION_ID");
            var token = Environment.GetEnvironmentVariable("UNITYTOOLS_TOKEN");
            if (String.IsNullOrEmpty(session) || String.IsNullOrEmpty(token))
            {
                Log.LogInfo("UnityTools control inactive: game not launched by UT");
                return;
            }
            AddComponent<BridgeDispatcher>();
            access = new Il2CppObjectAccess();
            entries = new GameEntries { IsAlive = Il2CppObjectAccess.Alive, ResolveObject = access.ResolveForBridge, SameObject = Il2CppObjectAccess.Same, RegisterObject = access.RegisterForBridge };
            access.ObjectObserved = entries.RegisterProvider;
            protocol = new BridgeProtocol(session, token, System.Diagnostics.Process.GetCurrentProcess().Id,
                "il2cpp", action => Queue.Enqueue(action), Route);
            protocol.FrameRoute = FrameRoute;
            host = new HttpListenerHost(protocol.Handle, 0);
            host.Start();
            handshake = Environment.GetEnvironmentVariable("UNITYTOOLS_HANDSHAKE");
            if (!String.IsNullOrEmpty(handshake))
            {
                var directory = Path.GetDirectoryName(handshake);
                if (!String.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                var node = new JSONObject();
                node["sessionId"] = session;
                node["pid"] = System.Diagnostics.Process.GetCurrentProcess().Id;
                node["runtime"] = "il2cpp";
                node["port"] = host.Port;
                node["protocolVersion"] = "1";
                var temporary = handshake + ".tmp";
                File.WriteAllText(temporary, node.ToString(), new UTF8Encoding(false));
                File.Move(temporary, handshake, true);
            }
            Log.LogInfo("UnityTools session bridge listening on loopback port " + host.Port);
        }

        private static IEnumerable<JSONNode> FrameRoute(string path, JSONNode args)
        {
            if (path == "/objects/find") return access.FindFrames(args);
            if (path == "/objects/inspect") return access.InspectFrames(args);
            return SingleFrame(Route(path, args));
        }

        private static IEnumerable<JSONNode> SingleFrame(JSONNode value)
        {
            yield return value;
        }

        private static JSONNode Route(string path, JSONNode args)
        {
            if (path == "/capabilities")
            {
                var node = new JSONObject();
                node["runtime"] = "il2cpp";
                node["pid"] = System.Diagnostics.Process.GetCurrentProcess().Id;
                node["bridgeVersion"] = "1.0.0";
                node["protocolVersion"] = "1";
                foreach (var name in new[] {"objects", "members.read", "members.write", "methods.invoke"})
                    node[name] = Capability("available", "Il2CppInterop generated projections; unsupported members reported individually");
                var providers = entries.ProviderRows();
                var consoles = entries.DiscoverConsoles(AppDomain.CurrentDomain.GetAssemblies());
                node["playDataProviders"] = providers;
                node["gameConsoles"] = consoles;
                node["playData"] = Capability(providers.Count > 0 ? "available" : "unavailable", "explicit observed provider; snapshot only");
                node["gameConsole"] = Capability(consoles.Count > 0 ? "available" : "unavailable", "identified UnityIngameDebugConsole API only");
                node["translation.control"] = Capability("unavailable", "next-launch only with XUAT 5.6.2");
                return node;
            }
            if (path == "/playdata") return entries.ReadPlayData(args);
            if (path == "/cmd") return entries.SendCommand(args);
            if (path == "/objects/release" && args["all"].AsBool) entries.Clear();
            if (path.StartsWith("/objects/", StringComparison.Ordinal)) return access.Execute(path.Substring(9), args);
            if (path == "/translation/state") return TranslationState.Read();
            if (path == "/translation/set") return TranslationState.Set(args);
            return new JSONObject { ["state"] = "not-executed", ["error"] = "capability-unavailable" };
        }

        private static JSONNode Capability(string status, string reason)
        {
            return new JSONObject { ["status"] = status, ["reason"] = reason };
        }

        internal static void Stop()
        {
            protocol?.Close();
            host?.Stop();
            access?.Clear();
            entries?.Clear();
            try { if (!String.IsNullOrEmpty(handshake)) File.Delete(handshake); } catch { }
        }
    }

    public sealed class BridgeDispatcher : MonoBehaviour
    {
        public BridgeDispatcher(IntPtr handle) : base(handle) { }
        private void Update()
        {
            Action action;
            if (UnityToolsBridgeIl2Cpp.Queue.TryDequeue(out action)) action();
        }
        private void OnApplicationQuit() { UnityToolsBridgeIl2Cpp.Stop(); }
    }
}
