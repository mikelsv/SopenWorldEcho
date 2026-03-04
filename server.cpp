#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>

#define _WINSOCK_DEPRECATED_NO_WARNINGS

// ── Платформо-зависимые заголовки ──────────────────────────────────────────
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#define CLOSE_SOCK(s) closesocket(s)
#define SOCK_ERR WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCK (-1)
#define CLOSE_SOCK(s) close(s)
#define SOCK_ERR errno
#endif

// ── Константы ──────────────────────────────────────────────────────────────
static constexpr int MAX_THREADS = 10;
static constexpr int BACKLOG = 16;
static constexpr int BUFSIZE = 4096;

// ── Глобальный счётчик активных потоков ───────────────────────────────────
static std::atomic<int>  g_active_threads{ 0 };
static std::mutex        g_cout_mx;

static void log(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_cout_mx);
    std::cerr << "[server] " << msg << "\n";
}

// ── Утилиты ────────────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ── Отправить строку клиенту ───────────────────────────────────────────────
static bool send_line(socket_t sock, const std::string& msg) {
    std::string out = msg + "\n";
    int total = 0;
    int len = static_cast<int>(out.size());
    while (total < len) {
        int sent = send(sock, out.c_str() + total, len - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

// ── Обработка одного клиента ──────────────────────────────────────────────
static void handle_client(socket_t client_sock) {
    log("Client connected (active=" + std::to_string(g_active_threads.load()) + ")");

    std::string buf;
    char chunk[BUFSIZE];

    while (true) {
        // Найти готовую строку в буфере
        size_t nl = buf.find('\n');
        if (nl == std::string::npos) {
            // Дочитать ещё данных
            int n = recv(client_sock, chunk, sizeof(chunk) - 1, 0);
            if (n <= 0) break;          // разрыв соединения
            chunk[n] = '\0';
            buf += chunk;
            continue;
        }

        std::string line = trim(buf.substr(0, nl));
        buf.erase(0, nl + 1);

        if (line.empty()) continue;

        std::string cmd = to_upper(line);

        if (cmd == "HELLO") {
            if (!send_line(client_sock, "HELLO, CLIENT!")) break;
        }
        else if (cmd.rfind("RANDOM", 0) == 0 &&
            (cmd.size() == 6 || cmd[6] == ' ')) {
            // Разбираем число после "RANDOM "
            long long count = 0;
            bool has_arg = (cmd.size() > 7);
            if (has_arg) {
                try {
                    count = std::stoll(cmd.substr(7));
                }
                catch (...) {
                    has_arg = false;
                }
            }
            if (!has_arg || count <= 0) {
                if (!send_line(client_sock,
                    "ERROR Usage: RANDOM <count>, count > 0")) break;
                continue;   // ждём следующую команду
            }

            // Таблица допустимых символов: 0-9, a-z, A-Z  (62 символа)
            static constexpr char kAlpha[] =
                "0123456789"
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            static constexpr int kAlphaLen = sizeof(kAlpha) - 1; // 62

            // Генерируем и шлём блоками, чтобы не выделять огромный буфер
            static constexpr long long kChunk = 65536;
            std::string chunk_buf;
            chunk_buf.reserve(static_cast<size_t>((std::min)(count, kChunk)));

            long long remaining = count;
            while (remaining > 0) {
                long long n = (std::min)(remaining, kChunk);
                chunk_buf.resize(static_cast<size_t>(n));
                for (long long i = 0; i < n; ++i)
                    chunk_buf[static_cast<size_t>(i)] = kAlpha[rand() % kAlphaLen];

                // send raw bytes (без \n — данные могут быть большими)
                const char* ptr = chunk_buf.data();
                long long   left = n;
                while (left > 0) {
                    int sent = send(client_sock, ptr,
                        static_cast<int>((std::min)(left,
                            static_cast<long long>(INT_MAX))), 0);
                    if (sent <= 0) goto client_disconnect;
                    ptr += sent;
                    left -= sent;
                }
                remaining -= n;
            }
            // Завершаем ответ переносом строки
            if (!send_line(client_sock, "")) break;
        }
        else if (cmd == "CLOSE") {
            send_line(client_sock, "BYE");
            break;
        }
        else {
            if (!send_line(client_sock, "UNKNOWN COMMAND: " + line)) break;
        }
    }

client_disconnect:
    CLOSE_SOCK(client_sock);
    --g_active_threads;
    log("Client disconnected (active=" + std::to_string(g_active_threads.load()) + ")");
}

// ── Разбор адреса: "ip:port" или просто "port" ────────────────────────────
static bool parse_address(const std::string& s,
    std::string& out_ip,
    uint16_t& out_port) {
    size_t colon = s.rfind(':');
    std::string port_str;
    if (colon != std::string::npos) {
        out_ip = s.substr(0, colon);
        port_str = s.substr(colon + 1);
    }
    else {
        out_ip = "0.0.0.0";
        port_str = s;
    }
    try {
        int p = std::stoi(port_str);
        if (p <= 0 || p > 65535) return false;
        out_port = static_cast<uint16_t>(p);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ── EchoServer ────────────────────────────────────────────────────────────
class EchoServer {
public:
    EchoServer() : server_sock_(INVALID_SOCK) {}
    ~EchoServer() { stop(); }

    bool start(const std::string& ip, uint16_t port) {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
#endif
        server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock_ == INVALID_SOCK) {
            std::cerr << "socket() failed: " << SOCK_ERR << "\n";
            return false;
        }

        // SO_REUSEADDR — быстрый перезапуск
        int yes = 1;
        setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (ip.empty() || ip == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        else {
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
        }

        if (bind(server_sock_,
            reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "bind() failed: " << SOCK_ERR << "\n";
            CLOSE_SOCK(server_sock_);
            return false;
        }

        if (listen(server_sock_, BACKLOG) < 0) {
            std::cerr << "listen() failed: " << SOCK_ERR << "\n";
            CLOSE_SOCK(server_sock_);
            return false;
        }

        log("Listening on " + ip + ":" + std::to_string(port)
            + "  (max threads: " + std::to_string(MAX_THREADS) + ")");
        return true;
    }

    void run() {
        srand(static_cast<unsigned>(time(nullptr)));

        while (true) {
            sockaddr_in client_addr{};
            socklen_t   client_len = sizeof(client_addr);

            socket_t client = accept(server_sock_,
                reinterpret_cast<sockaddr*>(&client_addr),
                &client_len);
            if (client == INVALID_SOCK) {
                log("accept() failed: " + std::to_string(SOCK_ERR));
                break;
            }

            // Ждём, пока освободится слот
            while (g_active_threads.load() >= MAX_THREADS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            ++g_active_threads;
            std::thread(handle_client, client).detach();
        }
    }

    void stop() {
        if (server_sock_ != INVALID_SOCK) {
            CLOSE_SOCK(server_sock_);
            server_sock_ = INVALID_SOCK;
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

private:
    socket_t server_sock_;
};

// ── main_server ───────────────────────────────────────────────────────────
int main_server(int argc, char* argv[]) {
    std::string address;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            address = argv[++i];
        }
        else if (arg.rfind("-p", 0) == 0 && arg.size() > 2) {
            address = arg.substr(2);          // -p8080
        }
        else if (arg.rfind("--port=", 0) == 0) {
            address = arg.substr(7);          // --port=8080
        }
        else if (arg.rfind("port=", 0) == 0) {
            address = arg.substr(5);          // port=8080
        }
    }

    if (address.empty()) {
        std::cerr << "Error: port not specified.\n"
            << "Usage: " << argv[0]
            << " -p <port|ip:port>\n"
            << "       " << argv[0]
            << " --port=<port|ip:port>\n";
        return 1;
    }

    std::string ip;
    uint16_t    port = 0;
    if (!parse_address(address, ip, port)) {
        std::cerr << "Error: invalid address '" << address << "'\n";
        return 1;
    }

    EchoServer srv;
    if (!srv.start(ip, port)) return 1;
    srv.run();
    return 0;
}

// ── Точка входа ───────────────────────────────────────────────────────────
//int main(int argc, char* argv[]) {
//    return main_server(argc, argv);
//}