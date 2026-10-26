// Local HTTP transport. No external libraries or Windows URL reservations required.
using System;
using System.IO;
using System.Text;
using System.Net;
using System.Net.Sockets;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
public sealed class CommandRequest {
    public string Method, Target, Host, Origin, Authorization, ContentType;
    public byte[] Body;
    public bool Local;
    internal TcpClient Client;
    public void Reply(int status, string type, byte[] body) {
        try {
            var stream = Client.GetStream();
            string label = status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 409 ? "Conflict" : "Error";
            string h = "HTTP/1.1 " + status + " " + label + "\r\nContent-Type: " + type + "\r\nContent-Length: " + body.Length + "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n\r\n";
            byte[] head = Encoding.ASCII.GetBytes(h); stream.Write(head, 0, head.Length); stream.Write(body, 0, body.Length);
        } catch { } finally { Client.Close(); }
    }
}
public sealed class CommandHttp : IDisposable {
    public ConcurrentQueue<CommandRequest> Queue = new ConcurrentQueue<CommandRequest>();
    readonly TcpListener listener;
    volatile bool running = true;
    int clients;
    public CommandHttp(int port, bool lan) {
        listener = new TcpListener(lan ? IPAddress.Any : IPAddress.Loopback, port); listener.Start(32);
        new Thread(Accept) { IsBackground = true }.Start();
    }
    void Accept() {
        while (running) {
            try { var c = listener.AcceptTcpClient(); if (Interlocked.Increment(ref clients) > 32) { Interlocked.Decrement(ref clients); c.Close(); continue; } ThreadPool.QueueUserWorkItem(Read, c); }
            catch { if (running) Thread.Sleep(50); }
        }
    }
    void Read(object state) {
        var c = (TcpClient)state; bool queued = false;
        try {
            c.ReceiveTimeout = 4000; c.SendTimeout = 8000; c.NoDelay = true;
            var s = c.GetStream(); var header = new MemoryStream(); int v; int end = 0;
            while ((v = s.ReadByte()) >= 0) {
                header.WriteByte((byte)v); if (header.Length > 16384) throw new IOException("Headers too large");
                if ((end == 0 || end == 2) && v == 13) end++; else if ((end == 1 || end == 3) && v == 10) end++; else end = 0;
                if (end == 4) break;
            }
            if (end != 4) throw new IOException("Incomplete headers");
            string[] lines = Encoding.ASCII.GetString(header.ToArray()).Split(new[] { "\r\n" }, StringSplitOptions.None);
            string[] first = lines[0].Split(' '); if (first.Length != 3 || !first[1].StartsWith("/")) throw new IOException("Bad request");
            var fields = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 1; i < lines.Length; i++) { int colon = lines[i].IndexOf(':'); if (colon < 1) continue; string key = lines[i].Substring(0,colon); if (fields.ContainsKey(key)) throw new IOException("Duplicate header"); fields[key] = lines[i].Substring(colon+1).Trim(); }
            if (fields.ContainsKey("Transfer-Encoding")) throw new IOException("Chunked requests not supported");
            int length = 0; if (fields.ContainsKey("Content-Length") && (!int.TryParse(fields["Content-Length"], out length) || length < 0 || length > 24*1024*1024)) throw new IOException("Body too large");
            byte[] body = new byte[length]; int got = 0; while (got < length) { int n = s.Read(body, got, length-got); if (n == 0) throw new IOException("Incomplete body"); got += n; }
            var r = new CommandRequest { Client = c, Method = first[0], Target = first[1], Body = body, Local = IPAddress.IsLoopback(((IPEndPoint)c.Client.RemoteEndPoint).Address) };
            string value; r.Host = fields.TryGetValue("Host", out value) ? value : "";
            r.Origin = fields.TryGetValue("Origin", out value) ? value : "";
            r.Authorization = fields.TryGetValue("Authorization", out value) ? value : "";
            r.ContentType = fields.TryGetValue("Content-Type", out value) ? value : "";
            Queue.Enqueue(r); queued = true;
        } catch { } finally { Interlocked.Decrement(ref clients); if (!queued) c.Close(); }
    }
    public CommandRequest Next() { CommandRequest r; return Queue.TryDequeue(out r) ? r : null; }
    public void Dispose() { running = false; listener.Stop(); CommandRequest r; while (Queue.TryDequeue(out r)) r.Client.Close(); }
}
