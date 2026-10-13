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
export const disconnect: () => Promise<void>;
export const cancel: () => void;
export const version: () => string;
