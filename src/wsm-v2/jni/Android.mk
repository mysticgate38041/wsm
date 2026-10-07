LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_loader
LOCAL_SRC_FILES := loader.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
LOCAL_LDLIBS := -llog -ldl -lstdc++
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
include $(BUILD_SHARED_LIBRARY)

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
include $(CLEAR_VARS)
LOCAL_MODULE := wsm_arm64
LOCAL_SRC_FILES := ../payload/h64.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable
LOCAL_LDLIBS := -llog -ldl
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
include $(BUILD_SHARED_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_engine
LOCAL_SRC_FILES := engine.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
LOCAL_LDLIBS := -llog -ldl -lstdc++
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
include $(BUILD_SHARED_LIBRARY)
