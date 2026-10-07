using System;
using System.Collections.Generic;
using System.Threading;
using SimpleJSON;

namespace UnityTools
{
    public sealed class BridgeProtocol
    {
        private sealed class RequestRecord
        {
            public string fingerprint;
            public long deadline;
            public string state = "queued";
            public string response;
            public string outcome;
            public bool executing;
        }

        private readonly object gate = new object();
        private readonly string sessionId;
        private readonly string token;
        private readonly int pid;
        private readonly string runtime;
        private readonly Action<Action> enqueue;
        private readonly Func<string, JSONNode, JSONNode> route;
        private readonly Dictionary<string, RequestRecord> requests = new Dictionary<string, RequestRecord>();
        private readonly Dictionary<string, string> acknowledgedFingerprints = new Dictionary<string, string>();
        private readonly HashSet<string> cancelledBeforeArrival = new HashSet<string>();
        private bool closed;
        public Func<string, JSONNode, IEnumerable<JSONNode>> FrameRoute;

        public BridgeProtocol(string sessionId, string token, int pid, string runtime,
                              Action<Action> enqueue, Func<string, JSONNode, JSONNode> route)
        {
            this.sessionId = sessionId ?? "";
            this.token = token ?? "";
            this.pid = pid;
            this.runtime = runtime ?? "";
            this.enqueue = enqueue ?? throw new ArgumentNullException("enqueue");
            this.route = route ?? throw new ArgumentNullException("route");
        }

        private static long Now { get { return (long)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalMilliseconds; } }

        public string Handle(string method, string path, string body, IDictionary<string, string> headers)
        {
            var requestId = Header(headers, "X-UnityTools-Request");
            if (String.IsNullOrEmpty(sessionId) || String.IsNullOrEmpty(token) || String.IsNullOrEmpty(requestId)
                || Header(headers, "X-UnityTools-Session") != sessionId || Header(headers, "X-UnityTools-Token") != token)
                return Envelope(requestId, "not-executed", null, "unauthorized");

            var control = path == "/requests/cancel" || path == "/requests/status" || path == "/requests/ack";
            var readOnly = path == "/capabilities" || path == "/playdata" || path == "/translation/state";
            if ((control && method != "POST") || (!control && method != "POST" && !(readOnly && method == "GET")))
                return Envelope(requestId, "not-executed", null, "method-not-allowed");
            long deadline;
            if (!Int64.TryParse(Header(headers, "X-UnityTools-Deadline"), out deadline)) deadline = Now + 15000;
            var fingerprint = method + "\n" + path + "\n" + body;
            RequestRecord record;
            bool dispatch = false;
            lock (gate)
            {
                if (closed) return Envelope(requestId, "not-executed", null, "bridge-closed");
                if (path == "/requests/status") return Status(body, requestId);
                if (path == "/requests/ack") return Acknowledge(body, requestId);
                if (requests.TryGetValue(requestId, out record))
                {
                    if (record.fingerprint != fingerprint) return Envelope(requestId, "not-executed", null, "request-id-conflict");
                }
                else if (acknowledgedFingerprints.TryGetValue(requestId, out var acknowledged))
                {
                    if (acknowledged != fingerprint) return Envelope(requestId, "not-executed", null, "request-id-conflict");
                    return Envelope(requestId, "completed", null, "result-acknowledged");
                }
                else
                {
                    record = new RequestRecord { fingerprint = fingerprint, deadline = deadline };
                    requests.Add(requestId, record);
                    if (path == "/requests/cancel")
                        record.response = Cancel(body, requestId);
                    else if (cancelledBeforeArrival.Remove(requestId))
                        Finish(record, Envelope(requestId, "cancelled", null, "cancelled"), "cancelled");
                    else if (deadline <= Now)
                        Finish(record, Envelope(requestId, "not-executed", null, "deadline-expired"), "not-executed");
                    else dispatch = true;
                }
            }
            if (dispatch)
            {
                try { enqueue(() => Execute(requestId, path, body)); }
                catch (Exception error)
                {
                    lock (gate)
                    {
                        if (!record.executing && record.response == null)
                            Finish(record, Envelope(requestId, "not-executed", null, error.Message), "not-executed");
                    }
                }
            }
            lock (gate)
            {
                while (record.response == null)
                {
                    var remaining = record.deadline - Now;
                    if (remaining <= 0)
                    {
                        var state = record.executing ? "unknown" : "not-executed";
                        Finish(record, Envelope(requestId, state, null, "deadline-expired"), state);
                        break;
                    }
                    Monitor.Wait(gate, (int)Math.Min(remaining, Int32.MaxValue));
                }
                return record.response;
            }
        }

        private void Execute(string requestId, string path, string body)
        {
            RequestRecord record;
            lock (gate)
            {
                if (!requests.TryGetValue(requestId, out record) || record.response != null) return;
                if (closed || Now >= record.deadline)
                {
                    var earlyState = closed ? "cancelled" : "not-executed";
                    Finish(record, Envelope(requestId, earlyState, null, closed ? "bridge-closed" : "deadline-expired"), earlyState);
                    return;
                }
                record.executing = true;
                record.state = "executing";
            }
            string response;
            string state;
            JSONNode args;
            try
            {
                args = String.IsNullOrEmpty(body) ? new JSONObject() : JSON.Parse(body);
                if (args == null || !args.IsObject) throw new FormatException("arguments-must-be-object");
            }
            catch (Exception error)
            {
                response = Envelope(requestId, "not-executed", null, error.Message);
                lock (gate) { record.executing = false; record.outcome = response; if (record.response == null) Finish(record, response, "not-executed"); }
                return;
            }
            if (FrameRoute != null && (path == "/objects/find" || path == "/objects/inspect"))
            {
                try { ContinueFrames(requestId, record, FrameRoute(path, args).GetEnumerator(), null); }
                catch (Exception error) { CompleteFrames(requestId, record, null, error); }
                return;
            }
            try
            {
                var result = route(path, args);
                state = result == null || String.IsNullOrEmpty(result["state"].Value) ? "completed" : result["state"].Value;
                response = Envelope(requestId, state, result, null);
            }
            catch (Exception error)
            {
                // A route may already have entered a game setter/method. Only the
                // object accessor can prove a failure occurred before its side effect.
                state = "executed-unverified";
                response = Envelope(requestId, state, null, (error.InnerException ?? error).Message);
            }
            lock (gate)
            {
                record.executing = false;
                record.outcome = response;
                if (record.response == null) Finish(record, response, state);
                Monitor.PulseAll(gate);
            }
        }

        private void ContinueFrames(string id, RequestRecord record, IEnumerator<JSONNode> iterator, JSONNode snapshot)
        {
            lock (gate)
            {
                if (record.response != null || closed || Now >= record.deadline)
                {
                    record.executing = false;
                    if (record.response == null) Finish(record, Envelope(id, "cancelled", null, "discovery-cancelled"), "cancelled");
                    iterator.Dispose();
                    return;
                }
            }
            try
            {
                if (!iterator.MoveNext()) { iterator.Dispose(); CompleteFrames(id, record, snapshot ?? new JSONObject(), null); return; }
                if (iterator.Current != null) snapshot = iterator.Current;
                var nextSnapshot = snapshot;
                enqueue(() => ContinueFrames(id, record, iterator, nextSnapshot));
            }
            catch (Exception error) { iterator.Dispose(); CompleteFrames(id, record, null, error); }
        }

        private void CompleteFrames(string id, RequestRecord record, JSONNode snapshot, Exception error)
        {
            lock (gate)
            {
                record.executing = false;
                var state = error == null ? "completed" : "not-executed";
                var response = Envelope(id, state, snapshot, error == null ? null : (error.InnerException ?? error).Message);
                record.outcome = response;
                if (record.response == null) Finish(record, response, state);
                Monitor.PulseAll(gate);
            }
        }

        private string Cancel(string body, string requestId)
        {
            var id = TargetId(body);
            if (String.IsNullOrEmpty(id)) return Envelope(requestId, "not-executed", null, "request-id-required");
            RequestRecord target;
            if (!requests.TryGetValue(id, out target))
            {
                cancelledBeforeArrival.Add(id);
                return Envelope(requestId, "cancelled", null, "cancelled-before-arrival");
            }
            if (target.response != null) return Envelope(requestId, "completed", JSON.Parse(target.response), null);
            var state = target.executing ? "unknown" : "cancelled";
            Finish(target, Envelope(id, state, null, target.executing ? "cancelled-after-start" : "cancelled"), state);
            return Envelope(requestId, state, null, target.executing ? "cancelled-after-start" : "cancelled");
        }

        private string Status(string body, string requestId)
        {
            RequestRecord target;
            var id = TargetId(body);
            if (String.IsNullOrEmpty(id) || !requests.TryGetValue(id, out target))
                return Envelope(requestId, "not-executed", null, "request-not-found");
            var snapshot = new JSONObject();
            snapshot["requestId"] = id;
            snapshot["state"] = target.state;
            snapshot["executing"] = target.executing;
            if (target.outcome != null) snapshot["outcome"] = JSON.Parse(target.outcome);
            else if (target.response != null) snapshot["outcome"] = JSON.Parse(target.response);
            return Envelope(requestId, "completed", snapshot, null);
        }

        private string Acknowledge(string body, string requestId)
        {
            RequestRecord target;
            var id = TargetId(body);
            if (String.IsNullOrEmpty(id) || !requests.TryGetValue(id, out target))
                return Envelope(requestId, "not-executed", null, "request-not-found");
            if (target.executing || target.response == null) return Envelope(requestId, "not-executed", null, "request-not-finished");
            target.outcome = null;
            acknowledgedFingerprints[id] = target.fingerprint;
            requests.Remove(id);
            return Envelope(requestId, "completed", new JSONObject(), null);
        }

        public void Close()
        {
            lock (gate)
            {
                closed = true;
                acknowledgedFingerprints.Clear();
                foreach (var pair in requests)
                {
                    if (pair.Value.response != null) continue;
                    var state = pair.Value.executing ? "unknown" : "cancelled";
                    Finish(pair.Value, Envelope(pair.Key, state, null, "bridge-closed"), state);
                }
                Monitor.PulseAll(gate);
            }
        }

        private void Finish(RequestRecord record, string response, string state)
        {
            record.response = response;
            record.state = state;
            Monitor.PulseAll(gate);
        }

        private string Envelope(string requestId, string state, JSONNode result, string error)
        {
            var envelope = new JSONObject();
            envelope["sessionId"] = sessionId;
            envelope["pid"] = pid;
            envelope["runtime"] = runtime;
            envelope["requestId"] = requestId ?? "";
            envelope["state"] = state;
            if (!String.IsNullOrEmpty(error)) envelope["error"] = error;
            else envelope["result"] = result ?? new JSONObject();
            return envelope.ToString();
        }

        private static string TargetId(string body)
        {
            try { return JSON.Parse(body ?? "{}")["requestId"].Value; } catch { return null; }
        }

        private static string Header(IDictionary<string, string> headers, string name)
        {
            if (headers == null) return "";
            foreach (var item in headers) if (String.Equals(item.Key, name, StringComparison.OrdinalIgnoreCase)) return item.Value;
            return "";
        }
    }
}
