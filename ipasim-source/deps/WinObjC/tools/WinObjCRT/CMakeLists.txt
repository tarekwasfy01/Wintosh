set (SOURCE_FILES
    # From `WinObjCRT.vcxproj`.
    dll/dllmain.cpp)

add_library (WinObjCRT SHARED ${SOURCE_FILES})
add_prep_dep (WinObjCRT)

target_link_libraries (WinObjCRT PRIVATE
    # From `WinObjCRT.vcxproj`.
    "-def:${CMAKE_CURRENT_SOURCE_DIR}/dll/WinObjCRT_${CMAKE_BUILD_TYPE}.def")
