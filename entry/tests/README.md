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

Passing these tests does **not** establish ArkTS syntax/type legality, ArkUI layout,
SDK API or ABI compatibility, actual HUKS behavior, N-API scheduling, HAP packaging,
permissions behavior, signing/installability, or Apple TV/tvOS interoperability.
The application still requires DevEco + HarmonyOS SDK compilation and device tests.
