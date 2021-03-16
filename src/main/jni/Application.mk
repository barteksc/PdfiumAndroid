APP_STL := c++_shared
APP_CPPFLAGS += -fexceptions

#For ANativeWindow support
APP_PLATFORM = android-14

APP_ABI :=  armeabi-v7a \
            arm64-v8a \
            x86 \
            x86_64