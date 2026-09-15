cmake_minimum_required(VERSION 3.22)
project(Gothic2VRAndroid LANGUAGES NONE)

# Packaging only. Gradle builds the same engine used by the Windows baseline.
set(engine "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
include("${engine}/lib/Tempest/Engine/cmake/TempestAndroid.cmake")
set(OPENGOTHIC_APPLICATION_ID com.gothic2vr.quest)
set(OPENGOTHIC_APP_NAME "Gothic II VR")
set(OPENGOTHIC_ICON_COLOR "#739FD7")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/identity")
configure_file("${engine}/android/app/SetupActivity.java.in" "${generated}/java/org/opengothic/app/SetupActivity.java" @ONLY)
configure_file("${engine}/android/app/identity.xml.in" "${generated}/res/values/identity.xml" @ONLY)
configure_file("${engine}/android/app/shortcuts.xml.in" "${generated}/res/xml/shortcuts.xml" @ONLY)
if(NOT EXISTS "${GOTHIC2VR_DEPENDENCY_INIT}")
  message(FATAL_ERROR "Provide GOTHIC2VR_DEPENDENCY_INIT from tools/prepare-dependencies.ps1")
endif()

tempest_android_application(Gothic2VR
  APPLICATION_ID ${OPENGOTHIC_APPLICATION_ID}
  NATIVE_SOURCE_DIR "${engine}"
  NATIVE_TARGET Gothic2Notr
  LIBRARY_NAME opengothic
  VERSION_CODE 60
  VERSION_NAME 0.1.0-alpha
  PROPERTY_PREFIX gothic2vr
  SIGNING_ENV_PREFIX GOTHIC2VR
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/AndroidManifest.xml"
  JAVA_DIRS "${engine}/android/app/src/main/java" "${generated}/java"
  ASSET_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  RESOURCE_DIRS "${engine}/android/app/src/main/res" "${generated}/res"
  DEPENDENCIES com.google.android.material:material:1.13.0
  CMAKE_ARGUMENTS -DBUILD_SHARED_LIBS=OFF -DTEMPEST_BUILD_AUDIO=ON
    "-C${GOTHIC2VR_DEPENDENCY_INIT}" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
  CPP_FLAGS -std=c++20 -fvisibility=hidden -ffunction-sections -fdata-sections
  SHRINK_RELEASE ANDROIDX)

# Only MIT-licensed hand assets are packaged; purchased game data stays external.
