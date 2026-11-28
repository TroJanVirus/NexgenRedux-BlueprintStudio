# Blueprint workers and channels

Redux worker scripts run in their own AngelScript engine. The screen keeps ownership of the thread and its named communication channel. A worker has strings, arrays, maths, JSON, Data, Random and Channel, but no scene, rendering, audio or asset-loading APIs. On original Xbox, concurrency shares one CPU; it is not multicore acceleration.

## Minimal worker

Put the following in the screen's asset folder as `worker.as`:

```angelscript
void main()
{
    Channel@ results = Channel("example.results");
    int n = 7;
    results.push("" + (n * n));
}
```

In Blueprint Studio:

1. Declare two screen fields: `worker:Thread@` and `results:Channel@`.
2. In On Enter, create a named Channel with name `example.results`, store its result in the results field, create a Thread with script `asset:worker.as`, store its result in the worker field, and call Start on the saved Thread.
3. In Update, call the Channel Pop overload with a boolean return and string out pin. Connect its boolean result to Branch. Only consume the out value on the True execution path. This example returns `49`.
4. Keep the Thread and Channel references alive for as long as the operation needs them. Stop a longer-running worker when cancelling a job; a worker should cooperate by checking `Worker::stopping` inside long loops. A Thread is started once, so create a new one for another job.

Use separate channel names for independent jobs, screens or concurrent instances; named channels are shared. Empty text is a legitimate message, so prefer the boolean Pop overload over testing the string for emptiness. Use JSON text for structured results. Apply scene changes only on the main screen, not from a worker. Poll on Update instead of waiting on the game thread. A worker script still needs to be written separately; general visual worker-graph authoring and arbitrary reverse import are not provided by this beta.

## Audio migration

Upstream Redux now decodes WAV, Ogg and ADPCM at runtime. Convert MP3 and FLAC source files using the current Cook tool rather than expecting the game to play those formats directly. Samples and AudioStream handles can be retained in Blueprint screen fields. Keep streaming queues filled without blocking the game loop.

The release workflow runs native document, compiler, engine/runtime, sanitizer and desktop checks before packaging and publishing a new immutable prerelease.
