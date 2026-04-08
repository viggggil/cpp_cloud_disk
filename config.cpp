#include "config.h"

#include <cstdlib>
#include <cstring>

void Config::parse_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            thread_num = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sql_num = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            trig_mode = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            actor_model = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            close_log = std::atoi(argv[++i]);
        }
    }
}
