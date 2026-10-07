LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_loader
LOCAL_SRC_FILES := loader.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror
LOCAL_LDLIBS := -llog -ldl -lstdc++
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_engine
LOCAL_SRC_FILES := engine.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror
LOCAL_LDLIBS := -llog -lstdc++
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
include $(BUILD_SHARED_LIBRARY)
