export type PickedRom = {
  name: string;
  bytes: Uint8Array;
};

async function fileToPicked(file: File): Promise<PickedRom> {
  const buf = await file.arrayBuffer();
  return { name: file.name, bytes: new Uint8Array(buf) };
}

export async function pickRomFile(): Promise<PickedRom | null> {
  const w = window as Window & {
    showOpenFilePicker?: (opts?: unknown) => Promise<FileSystemFileHandle[]>;
  };
  if (typeof w.showOpenFilePicker === "function") {
    try {
      const handles = await w.showOpenFilePicker({
        multiple: false,
        types: [
          {
            description: "N64 ROM",
            accept: {
              "application/octet-stream": [".z64", ".n64", ".v64"],
            },
          },
        ],
      });
      const file = await handles[0].getFile();
      return fileToPicked(file);
    } catch (err) {
      if (err instanceof DOMException && err.name === "AbortError") {
        return null;
      }
      // Fall through to input picker if the API is present but blocked.
    }
  }

  return new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".z64,.n64,.v64,application/octet-stream";
    input.addEventListener("change", async () => {
      const file = input.files?.[0];
      if (!file) {
        resolve(null);
        return;
      }
      resolve(await fileToPicked(file));
    });
    input.click();
  });
}

export function romFromDrop(dt: DataTransfer | null): File | null {
  if (!dt) return null;
  if (dt.files && dt.files.length > 0) return dt.files[0];
  return null;
}
