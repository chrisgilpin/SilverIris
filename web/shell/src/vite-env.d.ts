/// <reference types="vite/client" />

interface FileSystemFileHandle {
  getFile(): Promise<File>;
}

declare module "*?raw" {
  const src: string;
  export default src;
}

declare module "*?url" {
  const src: string;
  export default src;
}
