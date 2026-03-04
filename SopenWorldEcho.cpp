#include <iostream>
#include <string>
#include <ctime>

struct Version {
    const char* ver;
    const char* date;
};

const char* PROJECTNAME = "SopenWorldEcho";
Version PROJECTVER[] = {
    // new version to up
    {"0.0.1.0", "04.03.2026 18:53"},
    {"0.0.0.1", "04.03.2026 18:00"}
};

int main_client(int argc, char* argv[]);
int main_server(int argc, char* argv[]);


int main(int argc, char* argv[])
{
    std::cout << PROJECTNAME << " v." << PROJECTVER[0].ver << " (" << PROJECTVER[0].date << ").\r\n";

    if (argc < 2) {
        std::cerr << "Usage: " << PROJECTNAME << " [-c|client|-s|server] [options...]" << std::endl;
        return 1;
    }

    std::string_view mode = argv[1];

    if (mode == "-c" || mode == "client") {
        return main_client(argc - 1, argv + 1);
    }
    else if (mode == "-s" || mode == "server") {
        return main_server(argc - 1, argv + 1);
    }
    else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
}