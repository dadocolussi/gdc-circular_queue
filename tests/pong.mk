TARGET := pong
TGT_PREREQS := ping
TGT_INCDIRS := ../src
TGT_DEFS := 
SOURCES := pingpong.cpp

define RUN_PING_AND_PONG
	$(TARGET_DIR)/ping&
	$(TARGET_DIR)/pong
endef

TGT_POSTMAKE := $(RUN_PING_AND_PONG)
