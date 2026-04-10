CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread

TARGET := server
SRCS := \
	main.cpp \
	config.cpp \
	webserver.cpp \
	http/http_conn.cpp \
	http/http_parser.cpp \
	http/auth_manager.cpp \
	http/json_utils.cpp \
	http/file_service.cpp \
	timer/lst_timer.cpp \
	log/log.cpp \
	CGImysql/metadata_store.cpp \
	CGImysql/sql_connection_pool.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lmysqlclient -lcrypt

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
