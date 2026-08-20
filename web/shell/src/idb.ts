/** Origin-scoped IndexedDB. Packs stay on this origin; ROM bytes are not stored. */

export const DB_NAME = "silveriris";
export const DB_VERSION = 1;

const STORE_KV = "kv";
const STORE_PACKS = "packs";
const STORE_ROMS = "roms";
const STORE_SAVES = "saves";

let dbPromise: Promise<IDBDatabase> | null = null;

function openDb(): Promise<IDBDatabase> {
  if (typeof indexedDB === "undefined") {
    return Promise.reject(new Error("indexedDB unavailable"));
  }
  if (!dbPromise) {
    dbPromise = new Promise((resolve, reject) => {
      const req = indexedDB.open(DB_NAME, DB_VERSION);
      req.onupgradeneeded = () => {
        const db = req.result;
        if (!db.objectStoreNames.contains(STORE_KV)) {
          db.createObjectStore(STORE_KV);
        }
        if (!db.objectStoreNames.contains(STORE_PACKS)) {
          db.createObjectStore(STORE_PACKS);
        }
        if (!db.objectStoreNames.contains(STORE_ROMS)) {
          db.createObjectStore(STORE_ROMS);
        }
        if (!db.objectStoreNames.contains(STORE_SAVES)) {
          db.createObjectStore(STORE_SAVES);
        }
      };
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error ?? new Error("idb open failed"));
    });
  }
  return dbPromise;
}

export async function kvGet<T>(key: string): Promise<T | undefined> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_KV, "readonly");
    const req = tx.objectStore(STORE_KV).get(key);
    req.onsuccess = () => resolve(req.result as T | undefined);
    req.onerror = () => reject(req.error);
  });
}

export async function kvSet(key: string, value: unknown): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_KV, "readwrite");
    tx.objectStore(STORE_KV).put(value, key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export type StoredPack = {
  region: string;
  romSha1: string;
  created: number;
  blob: ArrayBuffer;
};

export async function packPut(packHash: string, value: StoredPack): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PACKS, "readwrite");
    tx.objectStore(STORE_PACKS).put(value, packHash);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function packGet(packHash: string): Promise<StoredPack | undefined> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PACKS, "readonly");
    const req = tx.objectStore(STORE_PACKS).get(packHash);
    req.onsuccess = () => resolve(req.result as StoredPack | undefined);
    req.onerror = () => reject(req.error);
  });
}

/** Test helper. */
export function resetIdbCache(): void {
  dbPromise = null;
}
