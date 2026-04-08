#ifndef CONFIG_H
#define CONFIG_H

class Config {
public:
    int port = 9006;
    int thread_num = 8;
    int sql_num = 8;
    int trig_mode = 0;
    int actor_model = 0;
    int close_log = 0;

    void parse_arg(int argc, char* argv[]);
};

#endif
