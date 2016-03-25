BUILD_DIR := objects
TARGET_DIR := products
CFLAGS := -O0 -g3 -std=c11 -pedantic -Wall -Wextra -Wshadow
CXXFLAGS := -O0 -g3 -std=c++11 -pedantic -Wall -Wextra -Wshadow
DEFS := _XOPEN_SOURCE=500
LDLIBS := -lrt
LDFLAGS := -pthread
SUBMAKEFILES := tests/sub.mk
