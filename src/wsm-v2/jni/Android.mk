LOCAL_PATH := $(call my-dir)

# Canonical Android build: keep loader/engine on both supported ABIs.
WSM_ENGINE_SOURCES := engine.cpp il2cpp_resolver.cpp aob_scanner.cpp hybrid_resolver.cpp \
    feature_flags.cpp dispatcher.cpp payload_worker.cpp
# Keep the statically linked C++ runtime private to each module's documented ABI.
WSM_LINK_FLAGS := -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_loader
LOCAL_SRC_FILES := module.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
LOCAL_LDLIBS := -llog -ldl
LOCAL_LDFLAGS := $(WSM_LINK_FLAGS)
include $(BUILD_SHARED_LIBRARY)

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
include $(CLEAR_VARS)
LOCAL_MODULE := wsm_arm64
LOCAL_SRC_FILES := ../payload/h64.cpp trampoline_pool.cpp
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable
LOCAL_LDLIBS := -llog -ldl
LOCAL_LDFLAGS := $(WSM_LINK_FLAGS)
include $(BUILD_SHARED_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := wsm_engine
LOCAL_SRC_FILES := $(WSM_ENGINE_SOURCES)
LOCAL_CPPFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
LOCAL_LDLIBS := -llog -ldl
LOCAL_LDFLAGS := $(WSM_LINK_FLAGS)
include $(BUILD_SHARED_LIBRARY)
