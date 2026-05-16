LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE    := CameraSmoothAML
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp RW/RenderWare.cpp
LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG -std=c++17
LOCAL_CFLAGS += -DDONT_IMPLEMENT_STB
LOCAL_C_INCLUDES += ./. ./stb
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)
