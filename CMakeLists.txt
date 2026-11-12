cmake_minimum_required(VERSION 3.15)
project(mirage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- emulator + engine
add_library(mirage_engine STATIC
  src/emu/Cpu6809.cpp src/emu/Via6522.cpp src/emu/Wd1772.cpp src/emu/Es5503.cpp src/emu/Mirage.cpp
  src/engine/MirageEngine.cpp src/engine/DiskImage.cpp)
target_include_directories(mirage_engine PUBLIC src)

# Boot ROM + default boot disk compiled in.
set(MIRAGE_ROM ${CMAKE_SOURCE_DIR}/resources/rom/mirage.bin)
set(MIRAGE_BOOT_DISK ${CMAKE_SOURCE_DIR}/resources/disks/OS/os32.img)
set(MIRAGE_WORDMARK ${CMAKE_SOURCE_DIR}/resources/ui/wordmark.png)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/EmbeddedData.cpp
  COMMAND ${CMAKE_COMMAND} -DROM=${MIRAGE_ROM} -DDISK=${MIRAGE_BOOT_DISK} -DDISK_NAME=os32 -DWORDMARK=${MIRAGE_WORDMARK}
          -DOUT=${CMAKE_BINARY_DIR}/EmbeddedData.cpp -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${MIRAGE_ROM} ${MIRAGE_BOOT_DISK} ${MIRAGE_WORDMARK} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake)
add_library(mirage_embedded STATIC ${CMAKE_BINARY_DIR}/EmbeddedData.cpp)
target_include_directories(mirage_embedded PUBLIC src/plugin)

# Headless tools
add_executable(mirage_cli tools/mirage_cli.cpp)
target_link_libraries(mirage_cli PRIVATE mirage_engine)
add_executable(mirage_disk_check tests/disk_check.cpp)
target_link_libraries(mirage_disk_check PRIVATE mirage_engine)
add_executable(mirage_engine_check tests/engine_check.cpp)
target_link_libraries(mirage_engine_check PRIVATE mirage_engine mirage_embedded)

# ---------------------------------------------------------------- plug-in (DPF)
option(MIRAGE_BUILD_PLUGIN "Build the plug-ins and the standalone app" ON)
if(MIRAGE_BUILD_PLUGIN)
  foreach(entry "dpf-standalone-settings.patch=third_party/DPF" "dpf-swap-interval.patch=third_party/DPF")
    string(REPLACE "=" ";" entry "${entry}")
    list(GET entry 0 patch)
    list(GET entry 1 dir)
    execute_process(COMMAND git apply --reverse --check ${CMAKE_SOURCE_DIR}/patches/${patch}
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/${dir} RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
    if(NOT already EQUAL 0)
      execute_process(COMMAND git apply ${CMAKE_SOURCE_DIR}/patches/${patch}
                      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/${dir} RESULT_VARIABLE failed)
      if(failed)
        message(FATAL_ERROR "could not apply patches/${patch}")
      endif()
    endif()
  endforeach()

  add_subdirectory(third_party/DPF)
  if(APPLE)
    set(MIRAGE_FORMATS vst3 au clap jack)
    set(MIRAGE_DPF_OPTIONS FORCE_NATIVE_AUDIO_FALLBACK USE_FILE_BROWSER)
  elseif(WIN32)
    set(MIRAGE_FORMATS vst3 clap)
    set(MIRAGE_DPF_OPTIONS USE_FILE_BROWSER)
  else()
    set(MIRAGE_FORMATS vst3 clap lv2 jack)
    set(MIRAGE_DPF_OPTIONS USE_FILE_BROWSER)
  endif()

  dpf_add_plugin(Mirage ${MIRAGE_DPF_OPTIONS} MONOLITHIC
    TARGETS ${MIRAGE_FORMATS}
    UI_TYPE opengl
    FILES_DSP src/plugin/MiragePlugin.cpp
    FILES_UI src/plugin/MirageUI.cpp)
  target_include_directories(Mirage PUBLIC src/plugin)
  target_link_libraries(Mirage PUBLIC mirage_engine mirage_embedded)

  # Disk library copied into every bundle's Resources/Disks.
  set(MIRAGE_DISK_DIR ${CMAKE_SOURCE_DIR}/resources/disks)
  function(mirage_copy_disks target dest)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${dest}
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${MIRAGE_DISK_DIR} ${dest})
  endfunction()

  if(APPLE AND TARGET Mirage-jack)
    target_sources(Mirage-jack PRIVATE src/plugin/mac/StandaloneSettings.mm)
    set_source_files_properties(src/plugin/mac/StandaloneSettings.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
    target_link_libraries(Mirage-jack PRIVATE "-framework Cocoa")
    target_include_directories(Mirage-jack PRIVATE src/plugin)
    set_target_properties(Mirage-jack PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/plugin/mac/StandaloneInfo.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "Mirage"
      MACOSX_BUNDLE_EXECUTABLE_NAME "Mirage"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.leonardolopezchavira.mirage"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    mirage_copy_disks(Mirage-jack ${CMAKE_BINARY_DIR}/bin/Mirage.app/Contents/Resources/Disks)
    # App icon (resources/icon, drawn by tools/make_icon.py)
    add_custom_command(TARGET Mirage-jack POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/resources/icon/Mirage.icns
              ${CMAKE_BINARY_DIR}/bin/Mirage.app/Contents/Resources/Mirage.icns)
  elseif(TARGET Mirage-jack)
    mirage_copy_disks(Mirage-jack ${CMAKE_BINARY_DIR}/bin/Disks)
  endif()
  if(TARGET Mirage-au)
    mirage_copy_disks(Mirage-au ${CMAKE_BINARY_DIR}/bin/Mirage.component/Contents/Resources/Disks)
  endif()
  if(TARGET Mirage-vst3)
    mirage_copy_disks(Mirage-vst3 ${CMAKE_BINARY_DIR}/bin/Mirage.vst3/Contents/Resources/Disks)
  endif()
  if(TARGET Mirage-clap AND APPLE)
    mirage_copy_disks(Mirage-clap ${CMAKE_BINARY_DIR}/bin/Mirage.clap/Contents/Resources/Disks)
  endif()
endif()
