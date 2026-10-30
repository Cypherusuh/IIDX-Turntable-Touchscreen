using System.Collections.Concurrent;
using System.Diagnostics;

internal static class TurntableDiagnostics
{
    private sealed class Aggregate
    {
        public long Count;
        public string LastDetails = "";
    }

    private static readonly ConcurrentQueue<string> PendingLines = new();
    private static readonly Dictionary<string, Aggregate> Aggregates = new();
    private static readonly object Gate = new();
    private static readonly AutoResetEvent Wake = new(false);
    private static readonly bool Verbose =
        Environment.GetEnvironmentVariable("MOUSEDROID_TURNTABLE_VERBOSE") is "1" or "true";
    private static Thread? writer;
    private static bool stopping;

    public static void Start()
    {
        lock (Gate)
        {
            if (writer is not null)
                return;
            stopping = false;
            writer = new Thread(WriteLoop)
            {
                IsBackground = true,
                Name = "MouseDroid turntable diagnostics"
            };
            writer.Start();
        }

        Log("TURN-DIAG", $"diagnostics started; verbose={(Verbose ? "on" : "off")}");
    }

    public static void Log(string component, string message)
    {
        PendingLines.Enqueue(Format(component, message));
        Wake.Set();
    }

    public static void Record(string component, string eventName, string details)
    {
        if (Verbose)
        {
            Log(component, $"{eventName} {details}");
            return;
        }

        lock (Gate)
        {
            string key = component + "\0" + eventName;
            if (!Aggregates.TryGetValue(key, out Aggregate? aggregate))
            {
                aggregate = new Aggregate();
                Aggregates.Add(key, aggregate);
            }

            aggregate.Count++;
            aggregate.LastDetails = details;
        }
    }

    public static void Stop()
    {
        Thread? currentWriter;
        lock (Gate)
        {
            currentWriter = writer;
            if (currentWriter is null)
                return;
            stopping = true;
        }

        Wake.Set();
        currentWriter.Join();

        lock (Gate)
            writer = null;
    }

    private static string Format(string component, string message) =>
        $"[{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz}] [{component}] {message}";

    private static void WriteLoop()
    {
        var lastAggregateFlush = Stopwatch.StartNew();
        while (true)
        {
            Wake.WaitOne(400);
            DrainPending();

            if (lastAggregateFlush.ElapsedMilliseconds >= 400)
            {
                FlushAggregates();
                lastAggregateFlush.Restart();
            }

            lock (Gate)
            {
                if (!stopping)
                    continue;
            }

            DrainPending();
            FlushAggregates();
            return;
        }
    }

    private static void DrainPending()
    {
        while (PendingLines.TryDequeue(out string? line))
            WriteLine(line);
    }

    private static void FlushAggregates()
    {
        List<string> lines = new();
        lock (Gate)
        {
            foreach ((string key, Aggregate aggregate) in Aggregates)
            {
                int separator = key.IndexOf('\0');
                string component = key[..separator];
                string eventName = key[(separator + 1)..];
                lines.Add(Format(
                    "TURN-DIAG",
                    $"[{component}] {eventName} count={aggregate.Count} last={{{aggregate.LastDetails}}}"));
            }

            Aggregates.Clear();
        }

        foreach (string line in lines)
            WriteLine(line);
    }

    private static void WriteLine(string line)
    {
        try
        {
            Console.WriteLine(line);
        }
        catch (IOException exception)
        {
            Debug.WriteLine("Turntable diagnostics write failed: " + exception.Message);
        }
    }
}
