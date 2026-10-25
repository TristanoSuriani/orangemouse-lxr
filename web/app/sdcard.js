// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The SD card image, kept in IndexedDB in chunks so that a write by the
// firmware only stores the chunks it touched.

export const SECTOR_SIZE = 512;
export const SD_SECTORS = 32768;               // 16 MB
const CHUNK_SECTORS = 64;                      // 32 KB per IndexedDB record
const CHUNK_SIZE = CHUNK_SECTORS * SECTOR_SIZE;
const DB_NAME = 'lxr-web';
const STORE = 'sdcard';
const SAVE_DELAY_MS = 300;

function request(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export class SdCardStore {
  constructor() {
    this.image = new Uint8Array(SD_SECTORS * SECTOR_SIZE);
    this.dirtyChunks = new Set();
    this.saveTimer = null;
    this.saving = Promise.resolve();
    this.db = null;
  }

  async open() {
    if (this.db) return this.db;
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    this.db = await request(req);
    return this.db;
  }

  // Returns { image, blank }: blank when there is no saved card yet.
  async load() {
    let blank = true;
    try {
      const db = await this.open();
      const tx = db.transaction(STORE, 'readonly');
      const store = tx.objectStore(STORE);
      const [keys, values] = await Promise.all([request(store.getAllKeys()), request(store.getAll())]);
      keys.forEach((chunk, i) => {
        this.image.set(new Uint8Array(values[i]), chunk * CHUNK_SIZE);
        blank = false;
      });
    } catch (err) {
      console.warn('SD card storage unavailable, changes will not be kept', err);
    }
    return { image: this.image.slice(), blank };
  }

  write(sectors, data) {
    sectors.forEach((sector, i) => {
      this.image.set(data.subarray(i * SECTOR_SIZE, (i + 1) * SECTOR_SIZE), sector * SECTOR_SIZE);
      this.dirtyChunks.add(Math.floor(sector / CHUNK_SECTORS));
    });
    this.scheduleSave();
  }

  writeAll(data) {
    this.image.set(data);
    for (let c = 0; c < SD_SECTORS / CHUNK_SECTORS; c++) this.dirtyChunks.add(c);
    this.scheduleSave();
    return this.flush();
  }

  scheduleSave() {
    clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => this.flush(), SAVE_DELAY_MS);
  }

  flush() {
    clearTimeout(this.saveTimer);
    const chunks = [...this.dirtyChunks];
    this.dirtyChunks.clear();
    if (!chunks.length) return this.saving;
    this.saving = this.saving.then(async () => {
      try {
        const db = await this.open();
        const tx = db.transaction(STORE, 'readwrite');
        const store = tx.objectStore(STORE);
        for (const c of chunks) {
          const chunk = this.image.slice(c * CHUNK_SIZE, (c + 1) * CHUNK_SIZE);
          // all-zero chunks need no record
          if (chunk.some((b) => b !== 0)) store.put(chunk.buffer, c);
          else store.delete(c);
        }
        await new Promise((resolve, reject) => {
          tx.oncomplete = resolve;
          tx.onerror = () => reject(tx.error);
        });
      } catch (err) {
        console.warn('Could not save the SD card', err);
      }
    });
    return this.saving;
  }

  async snapshot() {
    await this.flush();
    return this.image.slice();
  }

  async erase() {
    clearTimeout(this.saveTimer);
    this.dirtyChunks.clear();
    await this.saving;
    const db = await this.open();
    const tx = db.transaction(STORE, 'readwrite');
    await request(tx.objectStore(STORE).clear());
  }
}

// Store pending changes when the page is closed or hidden.
export function flushOnHide(store) {
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') store.flush();
  });
}
