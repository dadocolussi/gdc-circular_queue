TARGET := c_impl_test
TGT_INCDIRS := ../src
TGT_DEFS := USE_C_API
SOURCES :=\
  main.cpp\
  factory.cpp\
  circular_queue.cpp\
  ../src/gdc_circular_queue.c\
  ../src/gdc_circular_queue_factory.c
TGT_POSTMAKE := $(TARGET_DIR)/$(TARGET)
