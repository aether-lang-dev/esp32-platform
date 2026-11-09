# RexxAE resource-interruption findings

Reproduced with Aether 0.878.0 and RexxAE 184ea83 in isolated copies. The original Aether and RexxAE checkouts were not modified.

## ABI outcome must be checked

On plain ESP32 without PSRAM, a script requesting a 307,200-byte string when the initial free heap was 276,344 bytes produced:

```
aether_rexxae_step: 0
aether_last_outcome: 5 (AETHER_OUT_OF_MEMORY)
reason: the call ran out of memory
```

The script is `SAY LENGTH(COPIES('<1024 x characters>',300))` followed by `EXIT 0`. Interpreting only the step return as REXXAE_DONE incorrectly classifies the interruption as success. Hosts must read `aether_last_outcome()` immediately after every export, before another export overwrites it. This applies to registration/reply APIs returning zero on success too.

Suggested RexxAE enhancement: explain this contract in include/rexxae.h and the C embedding examples, with tests that interrupt create, step and reply calls. Aether already exposes the required API; this is not evidence that Aether failed to detect OOM.

## Interrupted-call cleanup

Per-script execution accounts are already available in Aether and work on ESP32. With a 128 KiB account, repeated interrupted evaluations leaked about 5 KiB per context despite destroying the context afterwards. core_step loaded an owned instruction clone and freed it only after execute_ returned; interruption skipped that free.

The [partial patch](rexxae-unwind-partial.patch) scopes that clone with a defer. It also guards ownership transfers during lexer/context/public-handle creation. For the tested builtin interruption and a 7,207-byte dense assignment program interrupted during creation, the patched library returned to zero accounted bytes after destruction at the 128 KiB ceiling. The unguarded creation probe retained 123,099 accounted bytes.

Validation of the isolated patch: make test parity passed, including 513 strict reference comparisons. The instruction-clone fix also passed the unchanged ESP32 16-cycle failure/recovery/retention check: warm and final free heap were both 274,244 bytes.

This is **not complete allocation-failure hardening**. The [diagnostic sweep](../tools/probe_rexxae_resources.py) still finds retained allocations at other ceilings (including 34 bytes for the dense parse at 64 KiB, and larger leaks at some lower ceilings). Do not treat the partial patch as a complete fix or enable production resource isolation on that basis. Full Valgrind/memcheck validation was not run.

Run the supplied sweep against a self-contained library with public Aether runtime symbols:

```
python3 tools/probe_rexxae_resources.py /absolute/path/to/librexxae.dylib
```

It intentionally fails while any retained-account cases remain. Keep that failure visible while hardening the remaining paths. Aether's lower-level allocation-failure injection facilities can provide finer coverage than a memory-ceiling sweep.

Remaining failures are observed through RexxAE; each still needs attribution to engine ownership or an independently reproduced runtime issue. Do not infer an Aether compiler bug from this sweep alone. The diagnostic is deliberately failing and is not counted as a passing regression.
