using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace UnityTools
{
    internal sealed class HttpListenerHost
    {
        private readonly Func<string, string, string, IDictionary<string, string>, string> handler;
        private readonly int requestedPort;
        private TcpListener listener;
        private volatile bool running;
        public int Port { get; private set; }

        public HttpListenerHost(Func<string, string, string, IDictionary<string, string>, string> handler, int requestedPort)
        {
            this.handler = handler;
            this.requestedPort = requestedPort;
        }

        public void Start()
        {
            listener = new TcpListener(IPAddress.Loopback, requestedPort);
            listener.Start();
            Port = ((IPEndPoint)listener.LocalEndpoint).Port;
            running = true;
            new Thread(Loop) { IsBackground = true }.Start();
        }

        public void Stop()
        {
            running = false;
            try { listener?.Stop(); } catch { }
        }

        private void Loop()
        {
            while (running)
            {
                TcpClient client;
                try { client = listener.AcceptTcpClient(); }
                catch { if (!running) break; continue; }
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
                    stream.WriteTimeout = 15000;
                    var first = ReadLine(stream);
                    if (first == null) return;
                    var parts = first.Split(' ');
                    if (parts.Length != 3) return;
                    var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    int headerSize = first.Length;
                    while (true)
                    {
                        var line = ReadLine(stream);
                        if (line == null) return;
                        if (line.Length == 0) break;
                        headerSize += line.Length;
                        if (headerSize > 65536) return;
                        var colon = line.IndexOf(':');
                        if (colon <= 0) return;
                        headers[line.Substring(0, colon).Trim()] = line.Substring(colon + 1).Trim();
                    }
                    string length;
                    int contentLength = 0;
                    if (headers.TryGetValue("Content-Length", out length)
                        && (!Int32.TryParse(length, out contentLength) || contentLength < 0)) return;
                    var body = new byte[contentLength];
                    var received = 0;
                    while (received < contentLength)
                    {
                        var count = stream.Read(body, received, contentLength - received);
                        if (count <= 0) return;
                        received += count;
                    }
                    var responseBody = handler(parts[0], parts[1].Split('?')[0], Encoding.UTF8.GetString(body), headers);
                    var bytes = Encoding.UTF8.GetBytes(responseBody);
                    var responseHeader = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
                        + bytes.Length + "\r\nConnection: close\r\n\r\n");
                    stream.Write(responseHeader, 0, responseHeader.Length);
                    stream.Write(bytes, 0, bytes.Length);
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
                if (value < 0) return null;
                if (value == '\n') return result.ToString().TrimEnd('\r');
                result.Append((char)value);
                if (result.Length > 65536) return null;
            }
        }
    }
}
