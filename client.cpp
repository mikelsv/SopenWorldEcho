#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>

// ── Платформо-зависимые заголовки ──────────────────────────────────────────
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define INVALID_SOCK  INVALID_SOCKET
#define CLOSE_SOCK(s) closesocket(s)
#define SOCK_ERR      WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCK  (-1)
#define CLOSE_SOCK(s) close(s)
#define SOCK_ERR      errno
#endif

static constexpr int BUFSIZE = 65536;

// ══════════════════════════════════════════════════════════════════════════
// Утилиты
// ══════════════════════════════════════════════════════════════════════════

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool is_alnum_char(char c) {
    return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

// ══════════════════════════════════════════════════════════════════════════
// EchoClient
// ══════════════════════════════════════════════════════════════════════════

class EchoClient {
public:
    EchoClient() : sock_(INVALID_SOCK) {}
    ~EchoClient() { disconnect(); }

    // ── Подключение к серверу ───────────────────────────────────────────
    bool connect_to(const std::string& ip, uint16_t port) {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            log_err("WSAStartup failed");
            return false;
        }
#endif
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ == INVALID_SOCK) {
            log_err("socket() failed: " + std::to_string(SOCK_ERR));
            return false;
        }

        // Резолвим хост (поддерживаем как IP так и hostname)
        struct addrinfo hints {}, * res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        std::string port_str = std::to_string(port);
        if (getaddrinfo(ip.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
            log_err("getaddrinfo() failed for '" + ip + "'");
            CLOSE_SOCK(sock_); sock_ = INVALID_SOCK;
            return false;
        }

        log("Connecting to " + ip + ":" + port_str + " ...");
        auto t0 = now_ms();
        if (::connect(sock_, res->ai_addr, static_cast<int>(res->ai_addrlen)) < 0) {
            freeaddrinfo(res);
            log_err("connect() failed: " + std::to_string(SOCK_ERR));
            CLOSE_SOCK(sock_); sock_ = INVALID_SOCK;
            return false;
        }
        freeaddrinfo(res);
        log("Connected  (+" + std::to_string(now_ms() - t0) + " ms)");
        return true;
    }

    void disconnect() {
        if (sock_ != INVALID_SOCK) {
            CLOSE_SOCK(sock_);
            sock_ = INVALID_SOCK;
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // ── Команда HELLO ───────────────────────────────────────────────────
    bool cmd_hello() {
        log(">>> HELLO");
        if (!send_line("HELLO")) return false;

        std::string resp;
        if (!recv_line(resp)) return false;
        log("<<< " + resp);

        bool ok = (resp == "HELLO, CLIENT!");
        log(ok ? "[HELLO] OK" : "[HELLO] UNEXPECTED response: " + resp);
        return ok;
    }

    // ── Команда RANDOM <count> ──────────────────────────────────────────
    bool cmd_random(long long count) {
        log(">>> RANDOM " + std::to_string(count));
        if (!send_line("RANDOM " + std::to_string(count))) return false;

        auto t0 = now_ms();

        // Читаем ровно count байт, затем финальный '\n'
        std::string data;
        data.reserve(static_cast<size_t>((std::min)(count, (long long)BUFSIZE)));

        long long remaining = count;
        char buf[BUFSIZE];

        while (remaining > 0) {
            int to_read = static_cast<int>(
                (std::min)(remaining, (long long)sizeof(buf)));
            int n = recv(sock_, buf, to_read, 0);
            if (n <= 0) {
                log_err("[RANDOM] Connection lost while reading data");
                return false;
            }
            data.append(buf, static_cast<size_t>(n));
            remaining -= n;
        }

        // Читаем завершающий '\n'
        {
            char nl = 0;
            int n = recv(sock_, &nl, 1, 0);
            if (n <= 0 || nl != '\n') {
                log_err("[RANDOM] Missing trailing newline");
                return false;
            }
        }

        long long elapsed = now_ms() - t0;

        // Валидация: все символы должны быть из [0-9a-zA-Z]
        long long bad = 0;
        for (char c : data)
            if (!is_alnum_char(c)) ++bad;

        long long received = static_cast<long long>(data.size());
        bool size_ok = (received == count);
        bool chars_ok = (bad == 0);

        log("<<< Received " + std::to_string(received) + " bytes"
            + "  (+" + std::to_string(elapsed) + " ms)");
        log("[RANDOM] Size  : " + std::string(size_ok ? "OK" : "MISMATCH")
            + "  (expected=" + std::to_string(count)
            + " got=" + std::to_string(received) + ")");
        log("[RANDOM] Chars : " + std::string(chars_ok ? "OK" : "FAIL")
            + (chars_ok ? "" : "  (" + std::to_string(bad) + " invalid chars)"));

        // Показываем краткий preview (первые 64 символа)
        std::string preview = data.substr(0, (std::min)((size_t)64, data.size()));
        log("[RANDOM] Preview: \"" + preview
            + (data.size() > 64 ? "..." : "") + "\"");

        return size_ok && chars_ok;
    }

    // ── Команда CLOSE ───────────────────────────────────────────────────
    bool cmd_close() {
        log(">>> CLOSE");
        if (!send_line("CLOSE")) return false;

        std::string resp;
        if (!recv_line(resp)) return false;
        log("<<< " + resp);

        bool ok = (resp == "BYE");
        log(ok ? "[CLOSE] OK" : "[CLOSE] UNEXPECTED response: " + resp);
        return ok;
    }

private:
    socket_t    sock_;
    std::string recv_buf_;   // буфер для recv_line

    // ── Отправить строку + \n ───────────────────────────────────────────
    bool send_line(const std::string& msg) {
        std::string out = msg + "\n";
        int total = 0, len = static_cast<int>(out.size());
        while (total < len) {
            int sent = send(sock_, out.c_str() + total, len - total, 0);
            if (sent <= 0) {
                log_err("send() failed: " + std::to_string(SOCK_ERR));
                return false;
            }
            total += sent;
        }
        return true;
    }

    // ── Принять одну строку до \n ───────────────────────────────────────
    bool recv_line(std::string& out) {
        char buf[4096];
        while (true) {
            size_t nl = recv_buf_.find('\n');
            if (nl != std::string::npos) {
                out = trim(recv_buf_.substr(0, nl));
                recv_buf_.erase(0, nl + 1);
                return true;
            }
            int n = recv(sock_, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                log_err("recv() failed / connection closed");
                return false;
            }
            buf[n] = '\0';
            recv_buf_.append(buf, static_cast<size_t>(n));
        }
    }

    // ── Лог ─────────────────────────────────────────────────────────────
    static void log(const std::string& msg) {
        std::cerr << "[client] " << msg << "\n";
    }
    static void log_err(const std::string& msg) {
        std::cerr << "[client][ERROR] " << msg << "\n";
    }

    // ── Время в мс ──────────────────────────────────────────────────────
    static long long now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};

// ══════════════════════════════════════════════════════════════════════════
// Разбор адреса  "ip:port"  или  "hostname:port"
// ══════════════════════════════════════════════════════════════════════════

static bool parse_address(const std::string& s,
    std::string& out_ip,
    uint16_t& out_port) {
    size_t colon = s.rfind(':');
    if (colon == std::string::npos) {
        std::cerr << "Error: address must be in format ip:port or host:port\n";
        return false;
    }
    out_ip = s.substr(0, colon);
    try {
        int p = std::stoi(s.substr(colon + 1));
        if (p <= 0 || p > 65535) throw std::range_error("");
        out_port = static_cast<uint16_t>(p);
        return true;
    }
    catch (...) {
        std::cerr << "Error: invalid port in '" << s << "'\n";
        return false;
    }
}

// ══════════════════════════════════════════════════════════════════════════
// main_client
// ══════════════════════════════════════════════════════════════════════════

int main_client(int argc, char* argv[]) {
    std::string address;
    long long   random_count = 1024;   // значение по умолчанию

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // -r / --random
        if ((arg == "-r" || arg == "--random") && i + 1 < argc) {
            try {
                random_count = std::stoll(argv[++i]);
            }
            catch (...) {
                std::cerr << "Error: invalid value for " << arg << "\n";
                return 1;
            }
        }
        else if (arg.rfind("--random=", 0) == 0) {
            try {
                random_count = std::stoll(arg.substr(9));
            }
            catch (...) {
                std::cerr << "Error: invalid value for --random\n";
                return 1;
            }
        }
        else if (arg.rfind("-r", 0) == 0 && arg.size() > 2) {
            try {
                random_count = std::stoll(arg.substr(2));   // -r4096
            }
            catch (...) {
                std::cerr << "Error: invalid value for -r\n";
                return 1;
            }
        }
        else if (arg[0] != '-') {
            address = arg;   // позиционный аргумент — адрес
        }
    }

    if (address.empty()) {
        std::cerr << "Error: server address not specified.\n"
            << "Usage: " << argv[0]
            << " <ip:port> [-r <count>|--random=<count>]\n";
        return 1;
    }

    if (random_count <= 0) {
        std::cerr << "Error: --random value must be > 0\n";
        return 1;
    }

    std::string  ip;
    uint16_t     port = 0;
    if (!parse_address(address, ip, port)) return 1;

    // ── Сессия ───────────────────────────────────────────────────────────
    EchoClient client;
    if (!client.connect_to(ip, port)) return 1;

    int failures = 0;

    if (!client.cmd_hello())             ++failures;
    if (!client.cmd_random(random_count)) ++failures;
    if (!client.cmd_close())             ++failures;

    client.disconnect();

    std::cerr << "\n[client] Session finished. "
        << (failures == 0
            ? "All commands OK."
            : std::to_string(failures) + " command(s) FAILED.")
        << "\n";

    return failures == 0 ? 0 : 1;
}