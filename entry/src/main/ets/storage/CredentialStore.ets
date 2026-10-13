import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { util } from '@kit.ArkTS';
import { BusinessError } from '@kit.BasicServicesKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { huks } from '@kit.UniversalKeystoreKit';

const STORE_NAME: string = 'companion_pairing_v1';
const KEY_ALIAS: string = 'companion_pairing_v1';
const ENVELOPE_PREFIX: string = 'v1:';
const AAD_DOMAIN: string = 'info.remote.appletv.harmony:pairing:v1:';
const NONCE_BYTES: number = 12;
const TAG_BYTES: number = 16;
const MAX_CREDENTIAL_BYTES: number = 4096;
const MAX_ENVELOPE_CHARS: number = 5503;
const SAFE_ERROR: string = '无法访问安全配对存储，请重试或重新配对';

/**
 * Pairing bytes never become a string: only v1:base64(nonce|ciphertext|tag) is stored.
 * HUKS generates/manages the AES-256 key; no key import/export or plaintext fallback.
 * API contract: OpenHarmony docs, huks-encryption-decryption-arkts.md (AES/GCM).
 * GCM uses HUKS_TAG_NONCE; finishSession appends a 16-byte authentication tag.
 *
 * Use on the UIAbility's one ArkTS event loop. All instances share an operation
 * queue; no other process, worker, or code may write this preferences file.
 * Cancellation is checked immediately before putSync and after asynchronous flush.
 * A cancellation during flush restores the previous cache value and flushes it.
 * Preferences has no conditional transaction: process death/disk failure between
 * those flushes can leave the encrypted candidate on disk. This is not a
 * crash-atomic cancellation guarantee. A failed rollback disables this process's
 * store rather than exposing uncertain state. No backup/restore integration or
 * BackupExtensionAbility is provided; adding one requires review of this store.
 * Callers must zero their input and returned native ArrayBuffers after use.
 */
export default class CredentialStore {
  private static operationTail: Promise<void> = Promise.resolve();
  private static pendingKeys: Set<string> = new Set<string>();
  private static unavailable: boolean = false;
  private context: common.UIAbilityContext;
  private store: preferences.Preferences | undefined = undefined;
  private base64: util.Base64Helper = new util.Base64Helper();

  constructor(context: common.UIAbilityContext) {
    this.context = context;
  }

  async initialize(): Promise<void> {
    try {
      await this.enqueue<void>(async (): Promise<void> => {
        if (CredentialStore.unavailable) {
          throw new Error(SAFE_ERROR);
        }
        if (this.store !== undefined) {
          return;
        }
        const options: preferences.Options = { name: STORE_NAME };
        const store: preferences.Preferences = preferences.getPreferencesSync(this.context, options);
        const lookup: huks.HuksOptions = { properties: this.storageProperties() };
        let exists: boolean = false;
        try {
          exists = await huks.isKeyItemExist(KEY_ALIAS, lookup);
        } catch (error) {
          // Only an explicitly absent key permits generation. Other HUKS failures
          // must not replace a key and silently invalidate existing identities.
          const failure: BusinessError = error as BusinessError;
          if (failure.code !== 12000011) {
            throw new Error(SAFE_ERROR);
          }
        }
        if (!exists) {
          // ArkTS cannot enumerate the native getAllSync Object with Object.keys.
          // This checks emptiness only; its values are encrypted envelopes.
          if (JSON.stringify(store.getAllSync()) !== '{}') {
            throw new Error(SAFE_ERROR);
          }
          const generation: huks.HuksOptions = {
            properties: this.keyProperties(huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_ENCRYPT |
              huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_DECRYPT)
          };
          await huks.generateKeyItem(KEY_ALIAS, generation);
        }
        this.store = store;
      });
    } catch (_) {
      throw new Error(SAFE_ERROR);
    }
  }

  /** Presence is only a UI hint. Only load() authenticates the stored identity. */
  contains(id: string): boolean {
    try {
      const key: string = this.preferenceKey(id);
      const store: preferences.Preferences = this.readyStore();
      return !CredentialStore.pendingKeys.has(key) && store.hasSync(key);
    } catch (_) {
      throw new Error(SAFE_ERROR);
    }
  }

  async load(id: string): Promise<ArrayBuffer | undefined> {
    try {
      return await this.enqueue<ArrayBuffer | undefined>(async (): Promise<ArrayBuffer | undefined> => {
        const key: string = this.preferenceKey(id);
        const store: preferences.Preferences = this.readyStore();
        const envelope: string | undefined = this.readEnvelope(store, key);
        if (envelope === undefined) {
          return undefined;
        }
        const packed: Uint8Array = this.decodeEnvelope(envelope);
        const nonce: Uint8Array = packed.slice(0, NONCE_BYTES);
        const ciphertext: Uint8Array = packed.slice(NONCE_BYTES, packed.length - TAG_BYTES);
        const tag: Uint8Array = packed.slice(packed.length - TAG_BYTES);
        const params: huks.HuksParam[] = this.cipherProperties(id, nonce, false);
        params.push({ tag: huks.HuksTag.HUKS_TAG_AE_TAG, value: tag });
        const plaintext: Uint8Array = await this.runCipher(params, ciphertext);
        try {
          if (plaintext.length !== ciphertext.length) {
            throw new Error(SAFE_ERROR);
          }
          const result: Uint8Array = new Uint8Array(plaintext.length);
          result.set(plaintext);
          return result.buffer as ArrayBuffer;
        } finally {
          plaintext.fill(0);
        }
      });
    } catch (_) {
      // A malformed envelope, lost key, or invalid authentication tag never
      // becomes "unpaired", a usable credential, or an automatic plaintext retry.
      throw new Error(SAFE_ERROR);
    }
  }

  async save(id: string, bytes: ArrayBuffer, isCurrent: () => boolean): Promise<boolean> {
    let plaintext: Uint8Array = new Uint8Array(0);
    try {
      if (bytes.byteLength < 1 || bytes.byteLength > MAX_CREDENTIAL_BYTES) {
        throw new Error(SAFE_ERROR);
      }
      // Own the temporary copy; the caller still owns and must wipe bytes.
      plaintext = new Uint8Array(bytes).slice();
      return await this.enqueue<boolean>(async (): Promise<boolean> => {
        const key: string = this.preferenceKey(id);
        const store: preferences.Preferences = this.readyStore();
        if (!isCurrent()) {
          return false;
        }
        let envelope: string = '';
        try {
          envelope = await this.encryptEnvelope(id, plaintext);
        } finally {
          // Do not retain plaintext while waiting for the preferences flush.
          plaintext.fill(0);
        }
        const previous: string | undefined = this.readEnvelope(store, key);
        if (!isCurrent()) {
          return false;
        }
        CredentialStore.pendingKeys.add(key);
        let restoring: boolean = false;
        try {
          // No await between the final guard and the synchronous cache mutation.
          store.putSync(key, envelope);
          await store.flush();
          if (!isCurrent()) {
            restoring = true;
            await this.restore(store, key, previous);
            return false;
          }
          return true;
        } catch (_) {
          if (!restoring) {
            await this.restore(store, key, previous);
          }
          throw new Error(SAFE_ERROR);
        } finally {
          CredentialStore.pendingKeys.delete(key);
        }
      });
    } catch (_) {
      throw new Error(SAFE_ERROR);
    } finally {
      plaintext.fill(0);
    }
  }

  async remove(id: string): Promise<void> {
    try {
      await this.enqueue<void>(async (): Promise<void> => {
        const key: string = this.preferenceKey(id);
        const store: preferences.Preferences = this.readyStore();
        if (!store.hasSync(key)) {
          return;
        }
        CredentialStore.pendingKeys.add(key);
        try {
          // Explicit Forget must remove even a malformed/non-string value.
          // getSync(key, '') cannot snapshot every ValueType: a mismatched type
          // returns the default. Do not validate or invent a rollback value.
          store.deleteSync(key);
          await store.flush();
        } catch (_) {
          // A failed flush has uncertain durability. Keep the cache entry
          // removed and block this process from reading/saving any identities.
          // On the next launch, Forget may be retried if the disk entry remains.
          CredentialStore.unavailable = true;
          throw new Error(SAFE_ERROR);
        } finally {
          CredentialStore.pendingKeys.delete(key);
        }
      });
    } catch (_) {
      throw new Error(SAFE_ERROR);
    }
  }

  private enqueue<T>(operation: () => Promise<T>): Promise<T> {
    const result: Promise<T> = CredentialStore.operationTail.then(operation);
    CredentialStore.operationTail = result.then((): void => {}, (): void => {});
    return result;
  }

  private readyStore(): preferences.Preferences {
    const store: preferences.Preferences | undefined = this.store;
    if (CredentialStore.unavailable || store === undefined) {
      throw new Error(SAFE_ERROR);
    }
    return store;
  }

  private readEnvelope(store: preferences.Preferences, key: string): string | undefined {
    if (!store.hasSync(key)) {
      return undefined;
    }
    const value: preferences.ValueType = store.getSync(key, '');
    if (typeof value !== 'string' || value.length === 0 || value.length > MAX_ENVELOPE_CHARS) {
      throw new Error(SAFE_ERROR);
    }
    return value;
  }

  private async restore(store: preferences.Preferences, key: string, previous: string | undefined): Promise<void> {
    try {
      if (previous === undefined) {
        store.deleteSync(key);
      } else {
        store.putSync(key, previous);
      }
      await store.flush();
    } catch (_) {
      CredentialStore.unavailable = true;
      throw new Error(SAFE_ERROR);
    }
  }

  private preferenceKey(id: string): string {
    if (id.length === 0 || id.length > 256) {
      throw new Error(SAFE_ERROR);
    }
    return 'identity.' + this.base64.encodeToStringSync(this.codeUnits(id));
  }

  /** Explicit UTF-16BE is injective even for unpaired surrogates in target IDs. */
  private codeUnits(value: string): Uint8Array {
    const bytes: Uint8Array = new Uint8Array(value.length * 2);
    for (let index: number = 0; index < value.length; index++) {
      const unit: number = value.charCodeAt(index);
      bytes[index * 2] = unit >>> 8;
      bytes[index * 2 + 1] = unit & 0xff;
    }
    return bytes;
  }

  private storageProperties(): huks.HuksParam[] {
    return [{
      tag: huks.HuksTag.HUKS_TAG_AUTH_STORAGE_LEVEL,
      value: huks.HuksAuthStorageLevel.HUKS_AUTH_STORAGE_LEVEL_CE
    }];
  }

  private keyProperties(purpose: number): huks.HuksParam[] {
    const params: huks.HuksParam[] = this.storageProperties();
    params.push({ tag: huks.HuksTag.HUKS_TAG_ALGORITHM, value: huks.HuksKeyAlg.HUKS_ALG_AES });
    params.push({ tag: huks.HuksTag.HUKS_TAG_KEY_SIZE, value: huks.HuksKeySize.HUKS_AES_KEY_SIZE_256 });
    params.push({ tag: huks.HuksTag.HUKS_TAG_PURPOSE, value: purpose });
    params.push({ tag: huks.HuksTag.HUKS_TAG_PADDING, value: huks.HuksKeyPadding.HUKS_PADDING_NONE });
    params.push({ tag: huks.HuksTag.HUKS_TAG_BLOCK_MODE, value: huks.HuksCipherMode.HUKS_MODE_GCM });
    return params;
  }

  private cipherProperties(id: string, nonce: Uint8Array, encrypt: boolean): huks.HuksParam[] {
    const purpose: number = encrypt ? huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_ENCRYPT :
      huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_DECRYPT;
    const params: huks.HuksParam[] = this.keyProperties(purpose);
    params.push({ tag: huks.HuksTag.HUKS_TAG_NONCE, value: nonce });
    params.push({ tag: huks.HuksTag.HUKS_TAG_ASSOCIATED_DATA, value: this.codeUnits(AAD_DOMAIN + id) });
    return params;
  }

  private async encryptEnvelope(id: string, plaintext: Uint8Array): Promise<string> {
    const nonce: Uint8Array = cryptoFramework.createRandom().generateRandomSync(NONCE_BYTES).data;
    if (nonce.length !== NONCE_BYTES) {
      throw new Error(SAFE_ERROR);
    }
    const encrypted: Uint8Array = await this.runCipher(this.cipherProperties(id, nonce, true), plaintext);
    if (encrypted.length !== plaintext.length + TAG_BYTES) {
      throw new Error(SAFE_ERROR);
    }
    const packed: Uint8Array = new Uint8Array(NONCE_BYTES + encrypted.length);
    packed.set(nonce, 0);
    packed.set(encrypted, NONCE_BYTES);
    return ENVELOPE_PREFIX + this.base64.encodeToStringSync(packed);
  }

  private decodeEnvelope(envelope: string): Uint8Array {
    if (!envelope.startsWith(ENVELOPE_PREFIX) || envelope.length > MAX_ENVELOPE_CHARS) {
      throw new Error(SAFE_ERROR);
    }
    const encoded: string = envelope.substring(ENVELOPE_PREFIX.length);
    const packed: Uint8Array = this.base64.decodeSync(encoded);
    if (packed.length <= NONCE_BYTES + TAG_BYTES ||
      packed.length > NONCE_BYTES + MAX_CREDENTIAL_BYTES + TAG_BYTES ||
      this.base64.encodeToStringSync(packed) !== encoded) {
      throw new Error(SAFE_ERROR);
    }
    return packed;
  }

  private async runCipher(params: huks.HuksParam[], input: Uint8Array): Promise<Uint8Array> {
    let handle: number | undefined = undefined;
    try {
      const initOptions: huks.HuksOptions = { properties: params };
      const session: huks.HuksSessionHandle = await huks.initSession(KEY_ALIAS, initOptions);
      handle = session.handle;
      const finishOptions: huks.HuksOptions = { properties: params, inData: input };
      const result: huks.HuksReturnResult = await huks.finishSession(handle, finishOptions);
      handle = undefined;
      if (result.outData === undefined) {
        throw new Error(SAFE_ERROR);
      }
      return result.outData;
    } catch (_) {
      if (handle !== undefined) {
        try {
          const abortOptions: huks.HuksOptions = { properties: [] };
          await huks.abortSession(handle, abortOptions);
        } catch (_) {
          // Abort is best effort. Never reveal raw platform errors or input.
        }
      }
      throw new Error(SAFE_ERROR);
    }
  }
}
