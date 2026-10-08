APP_ABI := x86_64 arm64-v8a
APP_PLATFORM := android-26
APP_STL := c++_static
APP_OPTIM := release
APP_CPPFLAGS := -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden
# ELF alignment is set per target in Android.mk; no third-party hook library is needed.
