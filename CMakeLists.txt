cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "VITASDK introuvable")
  endif()
endif()

project(heist_tests C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -std=gnu99")

set(LIBS
  vita2d
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  ScePgf_stub ScePvf_stub SceCommonDialog_stub ScePower_stub SceAppMgr_stub
  freetype png jpeg z m c
)

# Test de dessin (sprites, lightmap)
add_executable(heist_perf_test main.c)
target_link_libraries(heist_perf_test ${LIBS})
vita_create_self(heist_perf_test.self heist_perf_test)
vita_create_vpk(heist_perf_test.vpk HSTPERF01 heist_perf_test.self
  VERSION 01.00
  NAME "Perf Test 2D"
)

# Test CPU (animation, collisions, chemins, scripts)
add_executable(heist_cpu_test cpu_main.c)
target_link_libraries(heist_cpu_test ${LIBS})
vita_create_self(heist_cpu_test.self heist_cpu_test)
vita_create_vpk(heist_cpu_test.vpk HSTCPU001 heist_cpu_test.self
  VERSION 01.00
  NAME "Test CPU"
)
