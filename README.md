# OneDrive Crypto Uploader — v1.2 (Search + Breadcrumbs + Smart Save Name)

Cross-platform Qt 6 / C++20 GUI that encrypts files locally, uploads encrypted files to OneDrive (Microsoft Graph), and downloads + decrypts them.

## What’s new in v1.2

### ✅ Search box (filter current folder list)
- A search field filters the **current folder table** client-side via `QSortFilterProxyModel`.

### ✅ Clickable breadcrumb bar
- Breadcrumbs show your navigation path.
- Click any crumb to jump back to that folder.
- **Up** now goes to the parent crumb (not just root).

### ✅ Smart “Save As…” decrypted filename
- After downloading `downloaded.odenc`, the app reads ODENC metadata (original plaintext name) and proposes it as the default output filename.
- You can still pick any filename/location using a Save dialog.

---

## Setup

1) Copy config:

```bash
cp appconfig.example.json appconfig.json
```

2) Set your Entra app registration values in `appconfig.json`.

---

## Build

```bash
cmake -S . -B build -DODCU_ENABLE_TESTS=ON
cmake --build build -j
ctest --test-dir build
```

---

## Run

```bash
./build/onedrive_crypto_uploader
```

---

## Notes
- The OneDrive browser lists items in the current user drive.
- Folder listing is paged; the client follows `@odata.nextLink`.
