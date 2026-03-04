# EchoServer

A lightweight, cross-platform TCP echo server and client in a single binary. The server handles text-based commands over raw sockets, spawning a new thread per connection up to a configurable maximum. The client connects, runs a predefined command sequence, validates every response, and reports results.

---

## Features

- **Single binary, two modes** — switch between server and client with a flag
- **Cross-platform** — POSIX sockets on Linux/macOS, Winsock2 on Windows, no external dependencies
- **Multithreaded server** — up to 10 concurrent client threads (configurable at compile time)
- **Three built-in commands** — `HELLO`, `RANDOM`, `CLOSE`
- **Streaming RANDOM** — generates arbitrarily large payloads in 64 KB chunks without blowing the heap
- **Client validation** — checks response correctness, byte count, character set, and round-trip time

---

## Building

```bash
# Linux / macOS
g++ -std=c++17 -O2 -pthread SopenWorldEcho.cpp server.cpp client.cpp -o SopenWorldEcho

# Windows (MSVC)
cl /std:c++17 SopenWorldEcho.cpp server.cpp client.cpp Ws2_32.lib /Fe:SopenWorldEcho.exe
```

---

## Usage

```
SopenWorldEcho [-c|--client | -s|--server] [options...]
```

### Server mode

```bash
SopenWorldEcho -s -p <port>
SopenWorldEcho -s -p <ip:port>
SopenWorldEcho --server --port=0.0.0.0:8080
```

| Option | Description |
|---|---|
| `-p <addr>`, `--port <addr>`, `--port=<addr>` | Bind address — either `port` or `ip:port`. **Required.** |

### Client mode

```bash
SopenWorldEcho -c <ip:port>
SopenWorldEcho -c <ip:port> -r <count>
SopenWorldEcho --client 127.0.0.1:8080 --random=65536
```

| Option | Description |
|---|---|
| `<ip:port>` | Server address to connect to. **Required.** |
| `-r <n>`, `--random <n>`, `--random=<n>` | Number of bytes to request with `RANDOM`. Default: `1024`. |

---

## Protocol

All commands are plain text, terminated by `\n`. The server responds on the same line convention.

| Command | Server response |
|---|---|
| `HELLO` | `HELLO, CLIENT!` |
| `RANDOM <n>` | `<n>` random bytes from `[0-9a-zA-Z]` followed by `\n` |
| `CLOSE` | `BYE` then closes the connection |
| *(anything else)* | `UNKNOWN COMMAND: <input>` |

If `RANDOM` is called without a valid positive integer argument the server responds with `ERROR Usage: RANDOM <count>, count > 0` and keeps the connection open.

---

## Example session

**Terminal 1 — start the server:**
```
$ ./SopenWorldEcho --server -p 8080
[server] Listening on 0.0.0.0:8080  (max threads: 10)
[server] Client connected (active=1)
[server] Client disconnected (active=0)
```

**Terminal 2 — run the client:**
```
$ ./SopenWorldEcho --client 127.0.0.1:8080 --random=50000
[client] Connecting to 127.0.0.1:8080 ...
[client] Connected  (+1 ms)
[client] >>> HELLO
[client] <<< HELLO, CLIENT!
[client] [HELLO] OK
[client] >>> RANDOM 50000
[client] <<< Received 50000 bytes  (+4 ms)
[client] [RANDOM] Size  : OK  (expected=50000 got=50000)
[client] [RANDOM] Chars : OK
[client] [RANDOM] Preview: "aB3zK9mXpQ1..."
[client] >>> CLOSE
[client] <<< BYE
[client] [CLOSE] OK

[client] Session finished. All commands OK.
```

---

## Architecture notes

- **Thread pool cap** — the server busy-waits (50 ms poll) before accepting a new connection when all 10 slots are occupied; no connection is ever refused, it simply queues in the OS `listen` backlog.
- **`SO_REUSEADDR`** — the server socket is set before `bind` so the port is immediately reusable after a restart.
- **Buffered line reader** — both server and client accumulate TCP data in an internal string buffer and extract complete `\n`-terminated lines, handling partial TCP segments transparently.
- **Streaming writes** — `RANDOM` sends data in 64 KB chunks so the payload size is bounded in memory regardless of the requested count.

---

## License

MIT
