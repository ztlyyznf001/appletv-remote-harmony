/** The native worker serializes all Promise operations. cancel() aborts the current socket
 * and invalidates queued work immediately. No network I/O runs on ArkUI's thread.
 * Each rejected Promise carries a stable, non-sensitive Error.code string.
 */
export interface CompanionError extends Error { code: string; }
export const beginPairing: (host: string, port: number) => Promise<void>;
/** Returns opaque verified identity bytes. Caller must wipe after encryption/use. */
export const finishPairing: (pin: string) => Promise<ArrayBuffer>;
/** Resolves only after Pair Verify and encrypted session startup both succeed. */
export const connect: (host: string, port: number, credentials: ArrayBuffer) => Promise<void>;
/** Full key-down/key-up transaction. Supported codes: 1,2,3,4,5,6,7,14. */
export const press: (code: number) => Promise<void>;
/** Touch surface coordinates are integers in 0..1000. Phases: 1=begin, 3=move, 4=end.
 * Calls are serialized with button operations; begin must precede move/end.
 * Resolves after sending an encrypted event; touch events have no acknowledgement.
 */
export const touch: (x: number, y: number, phase: number) => Promise<void>;
/** Sends SELECT down while held, up on release. Repeated identical states are safe. */
export const holdSelect: (held: boolean) => Promise<void>;
/** Best-effort input release on a live session, then close; no release ACK is awaited. */
export const disconnect: () => Promise<void>;
export const cancel: () => void;
export const version: () => string;
