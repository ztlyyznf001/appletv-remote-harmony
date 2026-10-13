# App logic checks (not HarmonyOS compilation)

Run with Node.js 24 or newer, without downloading dependencies:

```sh
bash entry/tests/run-mocks.sh
```

The harness reads the actual `.ets` model/controller/discovery/storage sources,
removes imports and the state decorator, and uses Node's `stripTypeScriptTypes`.
It injects deterministic fake native, HUKS, preferences, mDNS and timer adapters.
The HUKS mock uses Node AES-GCM only to validate envelope and error-handling logic.
No network traffic, real pairing, key creation, or persistent device state is used.

Covered: numeric address parsing (IPv4/IPv6), mandatory dynamic port, duplicate
operations, stale completions, foreground/background cancellation, PIN validation,
verified-session state, at most two 3-second reconnect attempts, endpoint refresh
without changing stored identity, no authentication-error retries, no input replay,
confirmed target-bound forget, mDNS resolve cancellation, AES-GCM envelope/AAD,
corruption rejection, plaintext buffer ownership/wiping, save rollback and failure
handling. The harness includes success and nonzero-error discovery-start events.

Touch coverage uses the actual `TouchGesturePump.ets` and `RemoteController.ets`:
20,000-move bounded coalescing, one native Promise at a time, BEGIN/MOVE/END order,
integer coordinate clamping, END before tap SELECT, genuine held SELECT down/up
with Android's one-second minimum, immediate cancellation release, duplicate
gesture rejection, competing-button locks, cancellation during every async edge,
late completion/failure after reconnect, background/device-switch transport close,
timer cleanup, no replay after auth/network failure, and repeated gesture cleanup.
Mock clocks and native gates make these race cases deterministic; they do not
establish real packet delivery or tvOS hold behavior.

In-session mode/modal/pointer cancellation sends END and releases a held SELECT.
Background/disconnect invalidates the pump and immediately closes native transport
to protect a future session from old work. Remote receipt of release cannot be
guaranteed after transport closure or network loss; the native graceful-disconnect
path is tested independently.

Passing these tests does **not** establish ArkTS syntax/type legality, ArkUI layout,
SDK API or ABI compatibility, actual HUKS behavior, N-API scheduling, HAP packaging,
permissions behavior, signing/installability, or Apple TV/tvOS interoperability.
The application still requires DevEco + HarmonyOS SDK compilation and device tests.
