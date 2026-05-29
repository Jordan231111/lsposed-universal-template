#pragma once

#include <jni.h>

#include <string>

namespace native_utils {

/**
 * Registers the NativeUtils JNI surface on com.jordan.rogue.recovery.NativeUtils.
 * Called from template_native.cpp's JNI_OnLoad. Returns true on success.
 */
bool register_natives(JNIEnv *env);

/**
 * UTF-8 copy of a Java string. Returns "" on null input or JNI failure. The JNI critical
 * window is released before returning. Shared with template_native.cpp.
 */
std::string jstring_to_string(JNIEnv *env, jstring value);

}  // namespace native_utils
