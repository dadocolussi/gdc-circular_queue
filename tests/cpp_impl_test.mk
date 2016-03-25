TARGET := cpp_impl_test
TGT_INCDIRS := ../src
TGT_DEFS :=
SOURCES :=\
  main.cpp\
  factory.cpp\
  circular_queue.cpp
TGT_POSTMAKE := $(TARGET_DIR)/$(TARGET)
