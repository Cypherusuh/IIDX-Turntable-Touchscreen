using System.Collections.Concurrent;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.Pipes;
using System.Threading.Channels;
using System.Threading;
using HIDMaestro;

internal static class Program
{
    private const string MovementPipeName = "MouseDroidHIDBridge";
    private const string StatusPipeName = "MouseDroidHIDBridge.Status";

    private static readonly BlockingCollection<(int X, int Y)> Moves = new();
    private static readonly Channel<byte> StatusEvents = Channel.CreateUnbounded<byte>();
    private static readonly object OutputGate = new();
    private static readonly CancellationTokenSource ExitSignal = new();

    private static HMController? controller;
    private static int armed;

    private static bool IsArmed => Volatile.Read(ref armed) == 1;

    private static void SetArmed(bool value)
    {
        lock (OutputGate)
        {
            int newState = value ? 1 : 0;
            if (Interlocked.Exchange(ref armed, newState) == newState)
                return;

            if (value)
            {
                Console.WriteLine("Forwarding enabled for MouseDroid.");
                TurntableDiagnostics.Log("TURN-BRIDGE-STATE", "forwarding ARMED");
            }
            else
            {
                int queuedMoves = Moves.Count;
                while (Moves.TryTake(out _)) { }
                TurntableDiagnostics.Log(
                    "TURN-BRIDGE-STATE",
                    $"forwarding DISARMED; cleared queued movement count={queuedMoves}");
                if (controller is not null)
                    SubmitMouse(controller, 0, 0);
                Console.WriteLine("Forwarding stopped.");
            }

            if (!StatusEvents.Writer.TryWrite(value ? (byte)'A' : (byte)'D'))
            {
                Console.Error.WriteLine("Could not queue bridge status update.");
                TurntableDiagnostics.Log("TURN-BRIDGE-STATE", "failed to queue bridge status update");
            }
        }
    }

    private static void SubmitMouse(HMController target, int x, int y)
    {
        if (x != 0 || y != 0)
        {
            TurntableDiagnostics.Record(
                "TURN-HID",
                "SubmitMouse",
                $"X={x} Y={y}");
        }

        try
        {
            target.SubmitMouse(new HMMouseState { X = x, Y = y });
        }
        catch (Exception exception)
        {
            TurntableDiagnostics.Log(
                "TURN-HID",
                $"SubmitMouse failed X={x} Y={y}: {exception}");
            throw;
        }
    }

    private static async Task RunMovementPipeAsync()
    {
        var payload = new byte[8];

        while (!ExitSignal.IsCancellationRequested)
        {
            bool connected = false;
            await using var pipe = new NamedPipeServerStream(
                MovementPipeName,
                PipeDirection.In,
                1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);

            try
            {
                await pipe.WaitForConnectionAsync(ExitSignal.Token);
                connected = true;
                TurntableDiagnostics.Log("TURN-BRIDGE-PIPE", "movement pipe connected");
                while (!ExitSignal.IsCancellationRequested)
                {
                    int totalRead = 0;
                    while (totalRead < payload.Length)
                    {
                        int read = await pipe.ReadAsync(
                            payload.AsMemory(totalRead, payload.Length - totalRead),
                            ExitSignal.Token);
                        if (read == 0)
                        {
                            TurntableDiagnostics.Log("TURN-BRIDGE-PIPE", "movement pipe peer closed");
                            goto NextMovementConnection;
                        }

                        totalRead += read;
                    }

                    var movement = (
                        BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(0, 4)),
                        BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(4, 4)));

                    bool forwarded = false;
                    string rejectionReason = "forwarding-disabled";
                    int queueDepth = 0;
                    lock (OutputGate)
                    {
                        if (IsArmed && !Moves.IsAddingCompleted)
                        {
                            Moves.Add(movement);
                            forwarded = true;
                            rejectionReason = "";
                            queueDepth = Moves.Count;
                        }
                        else if (Moves.IsAddingCompleted)
                        {
                            rejectionReason = "queue-completed";
                        }
                    }

                    TurntableDiagnostics.Record(
                        "TURN-BRIDGE-PIPE",
                        forwarded ? "movement-forwarded" : "movement-discarded",
                        $"X={movement.Item1} Y={movement.Item2}" +
                            (forwarded ? $" queue={queueDepth}" : $" reason={rejectionReason}"));
                }
            }
            catch (OperationCanceledException) when (ExitSignal.IsCancellationRequested)
            {
                return;
            }
            catch (IOException ex)
            {
                Console.Error.WriteLine("Movement pipe error: " + ex.Message);
                TurntableDiagnostics.Log("TURN-BRIDGE-PIPE", "movement pipe error: " + ex.Message);
            }
            finally
            {
                if (connected)
                    TurntableDiagnostics.Log("TURN-BRIDGE-PIPE", "movement pipe disconnected");
            }

        NextMovementConnection:
            continue;
        }
    }

    private static async Task RunStatusPipeAsync()
    {
        while (!ExitSignal.IsCancellationRequested)
        {
            bool connected = false;
            await using var pipe = new NamedPipeServerStream(
                StatusPipeName,
                PipeDirection.Out,
                1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);

            try
            {
                await pipe.WaitForConnectionAsync(ExitSignal.Token);
                connected = true;
                TurntableDiagnostics.Log("TURN-BRIDGE-STATUSPIPE", "connected");
                while (StatusEvents.Reader.TryRead(out _)) { }
                byte initialState = IsArmed ? (byte)'A' : (byte)'D';
                await pipe.WriteAsync(new[] { initialState }, ExitSignal.Token);
                await pipe.FlushAsync(ExitSignal.Token);
                TurntableDiagnostics.Record(
                    "TURN-BRIDGE-STATUSPIPE",
                    "status-sent",
                    $"state={(char)initialState}");

                Task<byte> nextStatus = StatusEvents.Reader.ReadAsync(ExitSignal.Token).AsTask();
                while (!ExitSignal.IsCancellationRequested)
                {
                    Task heartbeat = Task.Delay(250, ExitSignal.Token);
                    byte state;
                    if (await Task.WhenAny(nextStatus, heartbeat) == nextStatus)
                    {
                        state = await nextStatus;
                        nextStatus = StatusEvents.Reader.ReadAsync(ExitSignal.Token).AsTask();
                    }
                    else
                    {
                        if (ExitSignal.IsCancellationRequested)
                            return;
                        state = (byte)'H';
                    }

                    await pipe.WriteAsync(new[] { state }, ExitSignal.Token);
                    await pipe.FlushAsync(ExitSignal.Token);
                    TurntableDiagnostics.Record(
                        "TURN-BRIDGE-STATUSPIPE",
                        "status-sent",
                        $"state={(char)state}");
                }
            }
            catch (OperationCanceledException) when (ExitSignal.IsCancellationRequested)
            {
                return;
            }
            catch (IOException ex)
            {
                Console.Error.WriteLine("Status pipe disconnected: " + ex.Message);
                TurntableDiagnostics.Log("TURN-BRIDGE-STATUSPIPE", "status pipe error: " + ex.Message);
                SetArmed(false);
            }
            finally
            {
                if (connected)
                    TurntableDiagnostics.Log("TURN-BRIDGE-STATUSPIPE", "disconnected");
            }
        }
    }

    private static async Task WatchParentAsync(int parentProcessId)
    {
        try
        {
            using var parent = Process.GetProcessById(parentProcessId);
            await parent.WaitForExitAsync(ExitSignal.Token);
            ExitSignal.Cancel();
        }
        catch (ArgumentException)
        {
            ExitSignal.Cancel();
        }
        catch (OperationCanceledException) when (ExitSignal.IsCancellationRequested)
        {
        }
    }

    public static void Main(string[] args)
    {
        using var logWriter = new StreamWriter(
            new FileStream(
                Path.Combine(AppContext.BaseDirectory, "MouseDroidHIDBridge.log"),
                FileMode.Append,
                FileAccess.Write,
                FileShare.ReadWrite))
        {
            AutoFlush = true
        };
        var synchronizedLogWriter = TextWriter.Synchronized(logWriter);
        Console.SetOut(synchronizedLogWriter);
        Console.SetError(synchronizedLogWriter);

        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            ExitSignal.Cancel();
        };

        int parentArgument = Array.IndexOf(args, "--parent-pid");
        if (parentArgument < 0 ||
            parentArgument + 1 >= args.Length ||
            !int.TryParse(args[parentArgument + 1], out int parentProcessId))
        {
            Console.Error.WriteLine("MouseDroid must start the HIDMaestro bridge.");
            return;
        }

        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 26100))
        {
            Console.Error.WriteLine("HIDMaestro requires Windows 11 build 26100 or later.");
            return;
        }

        try
        {
            using var parent = Process.GetProcessById(parentProcessId);
            TurntableDiagnostics.Start();
            using var context = new HMContext();
            context.LoadDefaultProfiles();

            var profile = context.GetProfile("logitech-gaming-mouse-g502");
            if (profile is null || !profile.IsDeployable)
                throw new InvalidOperationException("The HIDMaestro G502 mouse profile is missing or not deployable.");

            context.InstallDriver();

            using var activeController = context.CreateController(profile, "mousedroid-bridge");
            controller = activeController;
            SubmitMouse(activeController, 0, 0);
            SetArmed(true);
            Console.WriteLine("HIDMaestro forwarding started.");

            var worker = Task.Run(() =>
            {
                try
                {
                    while (!ExitSignal.IsCancellationRequested || Moves.Count > 0)
                    {
                        if (!Moves.TryTake(out var move, 100))
                            continue;

                        int remainingX = move.X;
                        int remainingY = move.Y;
                        while ((remainingX != 0 || remainingY != 0) &&
                               !ExitSignal.IsCancellationRequested)
                        {
                            int x = Math.Clamp(remainingX, -1000, 1000);
                            int y = Math.Clamp(remainingY, -1000, 1000);

                            lock (OutputGate)
                            {
                                if (!IsArmed)
                                    break;

                                SubmitMouse(activeController, x, y);
                            }

                            remainingX -= x;
                            remainingY -= y;
                            Thread.Sleep(1);
                        }
                    }
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine("HIDMaestro output worker failed: " + ex);
                    ExitSignal.Cancel();
                }
            });
            var movementPipeTask = Task.Run(RunMovementPipeAsync);
            var statusPipeTask = Task.Run(RunStatusPipeAsync);
            var parentTask = WatchParentAsync(parentProcessId);

            ExitSignal.Token.WaitHandle.WaitOne();
            SetArmed(false);
            ExitSignal.Cancel();
            Moves.CompleteAdding();
            worker.Wait();
            lock (OutputGate)
                SubmitMouse(activeController, 0, 0);
            Task.WaitAll(movementPipeTask, statusPipeTask, parentTask);
            controller = null;
            Console.WriteLine("HIDMaestro forwarding stopped.");
        }
        catch (ArgumentException ex)
        {
            Console.Error.WriteLine("MouseDroid parent process is unavailable: " + ex.Message);
        }
        catch (Exception ex)
        {
            ExitSignal.Cancel();
            Console.Error.WriteLine("HIDMaestro bridge failed: " + ex);
        }
        finally
        {
            TurntableDiagnostics.Stop();
        }
    }
}
