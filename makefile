# Makefile

# 编译器
CXX = g++

# 编译选项
CXXFLAGS = -std=c++17 -g -Wall

# 链接的库
LIBS = -lmuduo_base -lmuduo_net -lpthread

# 目标文件
TARGET = server

# 源文件列表（自动获取当前目录下所有 .cpp 文件）
SRCS = $(wildcard *.cpp)

# 对应的目标文件
OBJS = $(SRCS:.cpp=.o)

# 生成的目标文件规则
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LIBS)
	@rm -f $(OBJS) # 删除所有中间文件

# 编译规则
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理规则
clean:
	rm -f $(OBJS) $(TARGET)

# 伪目标
.PHONY: clean
