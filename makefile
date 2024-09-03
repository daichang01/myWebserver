# 设置要使用的 C++ 编译器，默认情况下如果没有先前设置，则为 g++。
CXX ?= g++

# 设置调试模式的开启与否。1 表示开启，这是默认设置。
DEBUG ?= 1

# 基于 DEBUG 变量的条件语句。
ifeq ($(DEBUG), 1)
    # 如果启用调试，编译时包含调试信息。
    CXXFLAGS += -g
else
    # 如果未启用调试，编译时使用优化级别 2。
    CXXFLAGS += -O2
endif

# 目标 'server' 依赖这些源文件。
server: main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp webserver.cpp ./config/config.cpp
	# 编译 server 可执行文件，链接 pthread 和 mysqlclient 库。
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient

# 目标 'clean' 用于清理编译出的输出。
clean:
	# 删除 server 可执行文件。
	rm -r server
