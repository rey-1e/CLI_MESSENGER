# Client Downloads Directory (`downloads/`)

This directory is the **client-side storage location** for files retrieved from the Concurrent FTP Server (`bin/ftp_server`) using the FTP client (`bin/ftp_client`).

---

## 1. Role in Client-Server Architecture

In a true Client-Server model, client storage is physically or logically decoupled from server storage:

| Partition | Role | Location | Description |
|---|---|---|---|
| **Server Storage** | Repository | `storage/` | Contains authoritative files hosted by the FTP server. |
| **Client Storage** | Receiver | `downloads/` | Destination where the client writes incoming file streams. |

Keeping downloads in `downloads/` ensures that testing file transfers never modifies or contaminates the server's authoritative `storage/` directory.

---

## 2. Default Download Workflow

When downloading a file without specifying an explicit output path:
```bash
# Downloads storage/demo.txt from server to downloads/demo.txt
./bin/ftp_client 127.0.0.1 9000 demo.txt
```

---

## 3. Verifying Integrity

To verify that the transferred file is bit-for-bit identical to the server original:
```bash
diff storage/demo.txt downloads/demo.txt
# If there is no output, the file was transferred with 100% integrity!
```
