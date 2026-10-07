// UnityToolsBridge — BepInEx 5 Mono runtime bridge for UnityTools Game Console.
// The network transport only authenticates and schedules requests. Every Unity
// object access is performed by the SynchronizationContext captured on Unity's
// main thread and returned as a JSON snapshot.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Threading;
using UnityEngine;
using BepInEx;
using BepInEx.Logging;
using SimpleJSON;

namespace UnityTools
{
    [BepInPlugin("unitytools.bridge", "UnityToolsBridge", "1.0.0")]
    public class UnityToolsBridge : BaseUnityPlugin
    {
        private static HttpListenerHost _host;
        private static SynchronizationContext _mainCtx;
        private static ManualLogSource _log;
        private static ObjectAccess _objectAccess;
        private static GameEntries _entries;
        private static BridgeProtocol _protocol;
        private static string _sessionId;
        private static string _token;
        private static string _handshakePath;
        private static bool _quittingHooked;

        private void Awake()
        {
            _log = Logger;
            _mainCtx = SynchronizationContext.Current;
            if (_objectAccess == null)
            {
                _objectAccess = new ObjectAccess();
                _objectAccess.IsAlive = IsAlive;
            }
            if (_entries == null)
            {
                _entries = new GameEntries();
                _entries.IsAlive = value => IsAlive(value) && (!(value is Component) || ((Component)value).gameObject != null && ((Component)value).gameObject.scene.isLoaded);
                _entries.ResolveObject = id => _objectAccess.ResolveForBridge(id);
                _entries.RegisterObject = _objectAccess.Register;
                _objectAccess.ObjectObserved = _entries.RegisterProvider;
            }
            if (_host == null)
            {
                _sessionId = Environment.GetEnvironmentVariable("UNITYTOOLS_SESSION_ID");
                _token = Environment.GetEnvironmentVariable("UNITYTOOLS_TOKEN");
                _handshakePath = Environment.GetEnvironmentVariable("UNITYTOOLS_HANDSHAKE");
                if (String.IsNullOrEmpty(_sessionId) || String.IsNullOrEmpty(_token) || String.IsNullOrEmpty(_handshakePath))
                {
                    _log.LogInfo("UnityTools control inactive: game was not launched by UT");
                    return;
                }
                int requestedPort;
                if (!Int32.TryParse(Environment.GetEnvironmentVariable("UNITYTOOLS_PORT"), out requestedPort)) requestedPort = 0;
                var host = new HttpListenerHost(HandleRequest, requestedPort);
                host.Start();
                _host = host;
                _objectAccess.Clear();
                _entries.Clear();
                _protocol = new BridgeProtocol(_sessionId, _token, ProcessId(), "mono",
                    action => {
                        var context = _mainCtx;
                        if (context == null) throw new InvalidOperationException("no-main-context");
                        context.Post(_ => action(), null);
                    }, Route);
                _protocol.FrameRoute = FrameRoute;
                WriteHandshake();
                if (!_quittingHooked)
                {
                    _quittingHooked = true;
                    Application.quitting += OnQuitting;
                }
                _log.LogWarning("[diag] session bridge started on loopback port " + _host.Port);
            }
            else
            {
                _log.LogWarning("[diag] bridge component re-instantiated; static session host retained");
            }
        }

        private static int ProcessId()
        {
            return System.Diagnostics.Process.GetCurrentProcess().Id;
        }

        private static void OnQuitting()
        {
            try { _protocol?.Close(); } catch { }
            try { _host?.Stop(); } catch { }
            _objectAccess?.Clear();
            _entries?.Clear();
            RemoveHandshake();
        }

        private static string HandleRequest(string method, string path, string body, IDictionary<string, string> headers)
        {
            var protocol = _protocol;
            if (protocol == null) return ErrorEnvelope("", "not-executed", "bridge-not-ready");
            return protocol.Handle(method, path, body, headers);
        }

        private static JSONNode Route(string path, JSONNode args)
        {
            if (path == "/capabilities") return CapabilitiesNode();
            if (path == "/playdata") return _entries.ReadPlayData(args);
            if (path == "/cmd") return _entries.SendCommand(args);
            if (path == "/objects/find") return FindObjectsNode(args);
            if (path == "/objects/release") {
                if (args["all"].AsBool) _entries.Clear();
                return _objectAccess.ExecuteNode("release", args);
            }
            if (path == "/objects/inspect" || path == "/objects/read"
                || path == "/objects/write" || path == "/objects/invoke")
                return _objectAccess.ExecuteNode(path.Substring("/objects/".Length), args);
            if (path == "/translation/state") return TranslationState.Read();
            if (path == "/translation/set") return TranslationState.Set(args);
            return Unavailable("not-found");
        }

        private static JSONNode CapabilitiesNode()
        {
            foreach (var component in Resources.FindObjectsOfTypeAll<MonoBehaviour>())
            {
                if (component != null && component.gameObject != null && component.gameObject.scene.isLoaded)
                    _entries.RegisterProvider(component, null);
            }
            var providers = _entries.ProviderRows();
            var consoles = _entries.DiscoverConsoles(AppDomain.CurrentDomain.GetAssemblies());
            var result = new JSONObject();
            result["runtime"] = "mono";
            result["pid"] = ProcessId();
            result["bridgeVersion"] = "1.0.0";
            result["protocolVersion"] = "1";
            result["port"] = _host == null ? 0 : _host.Port;
            result["objects"] = Capability("available", "UnityEngine reflection/object bridge");
            result["members.read"] = Capability("available", "Mono reflection");
            result["members.write"] = Capability("available", "Mono reflection with read-back");
            result["methods.invoke"] = Capability("available", "inspected non-generic non-ref/out methods");
            result["playDataProviders"] = providers;
            result["gameConsoles"] = consoles;
            result["playData"] = Capability(providers.Count > 0 ? "available" : "unavailable", "explicit GetCurrentPlayData provider selection; snapshot only");
            result["gameConsole"] = Capability(consoles.Count > 0 ? "available" : "unavailable", "identified UnityIngameDebugConsole API only; submission is not verification");
            result["translation.control"] = Capability("unavailable", "XUAT live control is not wired in this bridge");
            return result;
        }

        private static JSONObject Capability(string status, string reason)
        {
            var value = new JSONObject();
            value["status"] = status;
            value["reason"] = reason;
            return value;
        }

        private static JSONNode FindObjectsNode(JSONNode args)
        {
            JSONNode last = null;
            foreach (var frame in FindObjectsFrames(args)) last = frame;
            return last ?? Unavailable("no-runtime-target-found");
        }

        private static IEnumerable<JSONNode> FrameRoute(string path, JSONNode args)
        {
            if (path == "/objects/find") return FindObjectsFrames(args);
            if (path == "/objects/inspect") return _objectAccess.ExecuteFrames("inspect", args);
            return SingleFrame(Unavailable("not-frame-capable"));
        }

        private static IEnumerable<JSONNode> SingleFrame(JSONNode value)
        {
            yield return value;
        }

        private static IEnumerable<JSONNode> FindObjectsFrames(JSONNode args)
        {
            var query = args["type"].Value;
            var name = args["name"].Value;
            var candidates = new JSONArray();
            var unsupported = new JSONArray();

            if (String.IsNullOrEmpty(query))
            {
                var gameObjects = new List<GameObject>();
                try { gameObjects.AddRange(Resources.FindObjectsOfTypeAll<GameObject>()); }
                catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = "UnityEngine.GameObject", ["reason"] = error.Message }); }
                foreach (var gameObject in gameObjects)
                {
                    if (gameObject == null || !gameObject.scene.isLoaded || (!String.IsNullOrEmpty(name) && gameObject.name.IndexOf(name, StringComparison.OrdinalIgnoreCase) < 0)) continue;
                    try
                    {
                        var row = new JSONObject();
                        row["objectId"] = _objectAccess.Register(gameObject);
                        row["type"] = typeof(GameObject).AssemblyQualifiedName;
                        row["name"] = gameObject.name;
                        row["scene"] = gameObject.scene.path;
                        row["path"] = Hierarchy(gameObject);
                        candidates.Add(row);
                    }
                    catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = "UnityEngine.GameObject", ["reason"] = error.Message }); }
                    yield return FindResult(candidates, unsupported);
                }
                yield return FindResult(candidates, unsupported);
                yield break;
            }

            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type[] types;
                try { types = asm.GetTypes(); } catch { continue; }
                foreach (var type in types)
                {
                    var exact = String.Equals(type.FullName, query, StringComparison.Ordinal)
                        || String.Equals(type.Name, query, StringComparison.Ordinal)
                        || String.Equals(type.AssemblyQualifiedName, query, StringComparison.Ordinal);
                    if (!exact) continue;
                    if (typeof(Component).IsAssignableFrom(type))
                    {
                        var items = new List<UnityEngine.Object>();
                        try
                        {
                            foreach (var item in Resources.FindObjectsOfTypeAll(type)) items.Add(item);
                        }
                        catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = error.Message }); }
                        foreach (var item in items)
                        {
                            try
                            {
                                var component = item as Component;
                                if (component == null || component.gameObject == null || !component.gameObject.scene.isLoaded
                                    || (!String.IsNullOrEmpty(name) && component.gameObject.name.IndexOf(name, StringComparison.OrdinalIgnoreCase) < 0)) continue;
                                var row = new JSONObject();
                                row["objectId"] = _objectAccess.Register(item);
                                row["type"] = type.AssemblyQualifiedName;
                                row["name"] = component.gameObject.name;
                                row["scene"] = component.gameObject.scene.path;
                                row["path"] = Hierarchy(component.gameObject);
                                candidates.Add(row);
                            }
                            catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = error.Message }); }
                            yield return FindResult(candidates, unsupported);
                        }
                    }
                    else if (IsGameType(type))
                    {
                        try
                        {
                            var row = new JSONObject();
                            row["objectId"] = _objectAccess.RegisterType(type);
                            row["type"] = type.AssemblyQualifiedName;
                            row["name"] = type.Name;
                            row["path"] = type.FullName;
                            row["source"] = "static-type";
                            candidates.Add(row);
                        }
                        catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = error.Message }); }
                        yield return FindResult(candidates, unsupported);
                    }
                }
            }
            yield return FindResult(candidates, unsupported);
        }

        private static JSONNode FindResult(JSONArray candidates, JSONArray unsupported)
        {
            var result = new JSONObject();
            result["candidates"] = candidates;
            result["unsupported"] = unsupported;
            result["ambiguous"] = candidates.Count > 1;
            return result;
        }

        private static string Hierarchy(GameObject gameObject)
        {
            var path = gameObject.name;
            for (var parent = gameObject.transform.parent; parent != null; parent = parent.parent) path = parent.name + "/" + path;
            return path;
        }

        private static bool IsGameType(Type type)
        {
            var fullName = type == null ? "" : type.FullName ?? "";
            return !fullName.StartsWith("System.", StringComparison.Ordinal)
                && !fullName.StartsWith("Microsoft.", StringComparison.Ordinal)
                && !fullName.StartsWith("UnityEngine.", StringComparison.Ordinal)
                && !fullName.StartsWith("BepInEx.", StringComparison.Ordinal)
                && !fullName.StartsWith("Il2Cpp", StringComparison.Ordinal)
                && !typeof(MemberInfo).IsAssignableFrom(type)
                && !typeof(Assembly).IsAssignableFrom(type);
        }

        private static bool IsAlive(object value)
        {
            if (value is UnityEngine.Object) return (UnityEngine.Object)value != null;
            return value != null;
        }

        private static JSONNode Unavailable(string reason)
        {
            var result = new JSONObject();
            result["state"] = "not-executed";
            result["error"] = reason;
            return result;
        }

        private static string ErrorEnvelope(string requestId, string state, string reason)
        {
            var result = new JSONObject();
            result["sessionId"] = _sessionId ?? "";
            result["pid"] = ProcessId();
            result["requestId"] = requestId ?? "";
            result["state"] = state;
            result["error"] = reason;
            return result.ToString();
        }

        private static void WriteHandshake()
        {
            if (String.IsNullOrEmpty(_handshakePath) || _host == null) return;
            try
            {
                var directory = Path.GetDirectoryName(_handshakePath);
                if (!String.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                var temporary = _handshakePath + "." + Guid.NewGuid().ToString("N") + ".tmp";
                var handshake = new JSONObject();
                handshake["sessionId"] = _sessionId;
                handshake["pid"] = ProcessId();
                handshake["runtime"] = "mono";
                handshake["port"] = _host.Port;
                handshake["protocolVersion"] = "1";
                File.WriteAllText(temporary, handshake.ToString(), Encoding.UTF8);
                if (File.Exists(_handshakePath)) File.Replace(temporary, _handshakePath, null);
                else File.Move(temporary, _handshakePath);
            }
            catch (Exception error) { _log?.LogWarning("[diag] handshake write failed: " + error.Message); }
        }

        private static void RemoveHandshake()
        {
            if (String.IsNullOrEmpty(_handshakePath)) return;
            try { if (File.Exists(_handshakePath)) File.Delete(_handshakePath); } catch { }
        }
    }

    internal sealed class HttpListenerHost
    {
        private readonly Func<string, string, string, IDictionary<string, string>, string> _handler;
        private readonly int _requestedPort;
        private TcpListener _listener;
        private Thread _thread;
        private volatile bool _running;
        public int Port { get; private set; }

        public HttpListenerHost(Func<string, string, string, IDictionary<string, string>, string> handler, int requestedPort)
        {
            _handler = handler;
            _requestedPort = requestedPort;
        }

        public void Start()
        {
            _listener = new TcpListener(IPAddress.Loopback, _requestedPort);
            _listener.Start();
            Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
            _running = true;
            _thread = new Thread(Loop) { IsBackground = true };
            _thread.Start();
        }

        public void Stop()
        {
            _running = false;
            try { _listener?.Stop(); } catch { }
        }

        private void Loop()
        {
            while (_running)
            {
                TcpClient client;
                try { client = _listener.AcceptTcpClient(); }
                catch { if (!_running) break; continue; }
                ThreadPool.QueueUserWorkItem(_ => Handle(client));
            }
        }

        private void Handle(TcpClient client)
        {
            try
            {
                using (client)
                using (var stream = client.GetStream())
                {
                    stream.ReadTimeout = 15000;
                    var headerText = new StringBuilder();
                    var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    int contentLength = 0;
                    while (true)
                    {
                        var line = ReadLine(stream);
                        if (line == null) return;
                        if (line.Length == 0) break;
                        if (headerText.Length > 64 * 1024) return;
                        headerText.Append(line).Append('\n');
                        var colon = line.IndexOf(':');
                        if (colon > 0) headers[line.Substring(0, colon).Trim()] = line.Substring(colon + 1).Trim();
                        if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
                            Int32.TryParse(line.Substring(15).Trim(), out contentLength);
                    }
                    if (contentLength < 0 || contentLength > 1024 * 1024) return;
                    var bodyBytes = new byte[contentLength];
                    var received = 0;
                    while (received < contentLength)
                    {
                        var count = stream.Read(bodyBytes, received, contentLength - received);
                        if (count <= 0) break;
                        received += count;
                    }
                    var requestLine = headerText.ToString().Split('\n')[0].Split(' ');
                    var method = requestLine.Length > 0 ? requestLine[0] : "";
                    var path = requestLine.Length > 1 ? requestLine[1].Split('?')[0] : "";
                    var responseBody = _handler(method, path, Encoding.UTF8.GetString(bodyBytes, 0, received), headers);
                    var response = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                        + "Content-Length: " + Encoding.UTF8.GetByteCount(responseBody)
                        + "\r\nConnection: close\r\n\r\n" + responseBody;
                    var bytes = Encoding.UTF8.GetBytes(response);
                    stream.Write(bytes, 0, bytes.Length);
                    stream.Flush();
                }
            }
            catch { }
        }

        private static string ReadLine(NetworkStream stream)
        {
            var result = new StringBuilder();
            while (true)
            {
                var value = stream.ReadByte();
                if (value < 0) return result.Length == 0 ? null : result.ToString();
                if (value == '\n') return result.ToString().TrimEnd('\r');
                result.Append((char)value);
            }
        }
    }
}
