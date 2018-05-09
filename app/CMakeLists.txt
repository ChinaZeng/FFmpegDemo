# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.



add_library( # Sets the name of the library.
             native-lib

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             src/main/cpp/native-lib.cpp )
include_directories(src/main/cpp/include)

add_library(libavutil-55 SHARED IMPORTED)
set_target_properties(libavutil-55 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libavutil-55.so)

add_library(libswresample-2 SHARED IMPORTED)
set_target_properties(libswresample-2 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libswresample-2.so)

add_library(libavcodec-57 SHARED IMPORTED)
set_target_properties(libavcodec-57 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libavcodec-57.so)

add_library(libavformat-57 SHARED IMPORTED)
set_target_properties(libavformat-57 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libavformat-57.so)

add_library(libswscale-4 SHARED IMPORTED)
set_target_properties(libswscale-4 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libswscale-4.so)

add_library(libavfilter-6 SHARED IMPORTED)
set_target_properties(libavfilter-6 PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/${ANDROID_ABI}/libavfilter-6.so)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
              log-lib

              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
                       native-lib
                       libavutil-55
                       libswscale-4
                       libswresample-2
                       libavcodec-57
                       libavformat-57
                       libavfilter-6

                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib} )