# Server File Storage Repository (`storage/`)

This directory serves as the **central server-side file repository** for the Concurrent FTP Server (`bin/ftp_server`). It demonstrates server-side file system management, file isolation, and chunked TCP streaming in a client-server architecture.

---

## 1. Directory Purpose & Architecture

```
                    ┌────────────────────────┐
                    │   FTP Server Process   │
                    └───────────┬────────────┘
                                │ Serves files from:
                                ▼
                       storage/ (Server Repo)
                       ├── demo.txt   (Text)
                       └── image.jpg  (Binary)
                                │
             TCP Stream (Port 9000, 1024-byte chunks)
                                │
                                ▼
                     downloads/ (Client Storage)
                       └── demo.txt
```

- **Server Storage (`storage/`)**: The server's storage partition. Contains all files that remote clients are authorized to request and download.
- **Client Storage (`downloads/`)**: The client's local destination directory. When a client requests a file, the received bytes are saved into `downloads/`, keeping server storage clean and distinct.

---

## 2. Sample Assets Included

| File | Type | Size | Purpose |
|---|---|---|---|
| `demo.txt` | ASCII Text | ~4.4 KB | Tests text streaming, line-ending preservation, and EOF handling. |
| `image.jpg` | Binary JPEG | ~20.1 KB | Tests byte-exact binary chunking across multiple TCP segments. |

---

## 3. Hosting Custom Files

To make any new file available for download over the network:

1. Place the file inside this directory:
   ```bash
   cp /path/to/my_document.pdf storage/
   ```
2. The FTP server immediately makes `my_document.pdf` available — no server restart required:
   ```bash
   ./bin/ftp_client 127.0.0.1 9000 my_document.pdf
   # Downloaded to: downloads/my_document.pdf
   ```

---

## 4. Operating Systems Security & Isolation

- **Path Traversal Prevention**: Remote client requests are checked for directory breakout patterns (`..` or leading `/`). Any attempt to access files outside `storage/` (e.g. `/etc/passwd`) is immediately blocked by the server with an `ERR` response.
- **Concurrent Non-Interfering Reads**: Multiple child worker processes spawned via `fork()` open requested files with `O_RDONLY`, enabling fully parallel client streaming without race conditions or locks.
