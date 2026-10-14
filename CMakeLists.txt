cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "VITASDK introuvable")
  endif()
endif()

project(heist_perf_test C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Perf Test 2D")
set(VITA_TITLEID  "HSTPERF01")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -std=gnu99")

add_executable(${PROJECT_NAME} main.c)

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  ScePgf_stub ScePvf_stub SceCommonDialog_stub ScePower_stub SceAppMgr_stub
  freetype png jpeg z m c
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
