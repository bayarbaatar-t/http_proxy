# HTTP Proxy (C) — Coursework Build

A simple HTTP/1.1 forward proxy written in C for a university assignment. It parses client requests, forwards them to an origin server, and optionally caches responses using an in-memory LRU cache.

> **Environment note:** This project was designed, built, and tested in the **university lab environment**. It’s intentionally primitive and focuses on meeting the assignment spec rather than production parity with the modern web. See **Limitations** for details.

---

## Features (per assignment scope)
- Listens on a configurable port via `-p` (e.g., `-p 8080`). Optional `-c` enables the cache.
- Forwards HTTP/1.1 **GET** requests to the origin, which is dialed on **port 80** by design.
- Basic response caching with LRU eviction, `Cache-Control: max-age` handling, and stale detection.
- Clean separation of concerns: `src/` for code, `include/` for headers, simple `Makefile` builds.

---

## Quick start

### Native (Linux/WSL/MSYS2)
```bash
# release build
make

# run: listen on 8080, cache enabled
./htproxy -p 8080 -c
```

Open another terminal and test with a **plain HTTP** origin (not HTTPS):
```bash
curl -v -x http://127.0.0.1:8080 http://example.com/
```

> On Windows, use WSL or Docker. MSYS2/MinGW works too, but the code uses POSIX sockets.

### Docker
```Dockerfile
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends build-essential curl     && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make
EXPOSE 8080
CMD ["./htproxy", "-p", "8080", "-c"]
```
Build & run:
```bash
docker build -t htproxy .
docker run --rm -it -p 8080:8080 htproxy
# then, in another shell:
curl -v -x http://127.0.0.1:8080 http://example.com/
```

> **Port mapping:** `8080` is the **proxy’s listening port** (client → proxy). The proxy then connects outbound to the **origin on port 80** by design.

---

## How it works (map to files)

- **Entry point:** `main` parses flags `-p <port>` and optional `-c`, then calls `start_proxy(port, enable_cache)`.
- **Server loop:** `start_proxy` sets up a TCP listener (IPv6/IPv4-mapped), accepts clients, reads the HTTP request, extracts `Host` and request URI, and forwards the request to the origin.
- **Origin connect:** `connect_to_host(host)` uses `getaddrinfo` and connects to **port 80** (by spec).
- **Response read:** `read_from_server` reads headers and (when present) uses `Content-Length` to determine when the full body has arrived.
- **Forwarding:** `forward_request` writes the request to the origin, reads the response, and sends it back to the client; on success it returns the full response buffer/length for potential caching.
- **Cache:** LRU cache with max‐age and staleness detection; entries store request/response and metadata (`last_used`, `cached_time`, `max_age`).

---

## Repo layout
```
.
├─ src/
│  ├─ main.c        # CLI, starts proxy  (./htproxy -p <port> [-c])
│  ├─ proxy.c       # server loop, forwarding, origin connect
│  └─ cache.c       # LRU cache, Cache-Control handling
├─ include/
│  ├─ proxy.h       # BACKLOG, buffers, function prototypes
│  └─ cache.h       # cache structs and API
├─ Makefile
├─ Dockerfile       # build & run inside Debian container
├─ .gitignore       # ignore build artifacts / editor files
├─ .clang-format   # formatting rules for clang-format
└─ README.md
```

---

## Usage

**Start the proxy:**
```bash
./htproxy -p 8080 -c
```
- `-p <port>`: listening port for **clients → proxy**.
- `-c`: enable the in-memory cache (assignment stage 2).

**Make a request through it:**
```bash
curl -v -x http://127.0.0.1:8080 http://example.com/
```

> The proxy will connect to **example.com:80** (origin port is fixed at 80).

---

## Limitations (intentional for coursework)
- **No HTTPS tunneling (`CONNECT`)** — modern sites redirect to HTTPS, which isn’t supported here.  
- **No `Transfer-Encoding: chunked` decode** — the response reader and forwarder are geared towards `Content-Length` bodies; chunked/close-delimited responses may fail.  
- **Origin port fixed to 80** — the code always dials port 80 on the origin.  
- **Lab-functional assumption** — the code was validated against the uni harness that speaks plain HTTP on port 80 and uses `Content-Length`. In that environment, the proxy and cache paths worked as expected.

If you test against arbitrary public websites, you may hit errors like **“Empty reply from server.”** That’s expected given the scope above.

---

## Caching behavior (summary)
- On a cacheable response, the proxy stores the **full response buffer** and its **byte length**, tracks `last_used`, `cached_time`, and `max_age`.  
- The proxy checks `Cache-Control` for **no-cache / no-store** style directives and **max-age**; stale entries are evicted or refreshed.  
- Replacement policy is **LRU**: when full, the least-recently-used entry is evicted.  

---

## Development notes
- Headers expose only the necessary prototypes (`proxy.h`, `cache.h`).  
- Buffers: request `INIT_BUF_SIZE`, I/O `BUF_SIZE`, and cache sizing live in headers for clarity.  
- Server backlog and IPv6 (+v4-mapped) listener configured in the proxy server.  

---

## Roadmap (if extended later)
- Implement **`CONNECT`** to support HTTPS tunneling (and respect `Host: ...:port`).  
- Add support for **chunked** and **close-delimited** responses (streaming fallbacks).  
- Normalize client “proxy form” request line to origin “origin form” (`GET /path HTTP/1.1`) for compatibility with stricter origins.  
- Make the origin port **configurable** (parse `Host` header with optional `:port`).  
- CI: add GitHub Actions to build + run a tiny deterministic origin test.  

---

## Acknowledgements
- Built for a COMP30023 proxy assignment.  
- Networking patterns adapted from Beej’s Guide (as noted in comments).  

---

## Authors
- **Bayarbaatar (Bryan) Tuvshinbuyan**
- **Nazim Aminuddin**
