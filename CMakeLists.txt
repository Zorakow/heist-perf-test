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

# Test personnages (squelettes, deformation, envoi groupe)
add_executable(heist_chars_test chars_main.c)
target_link_libraries(heist_chars_test ${LIBS})
vita_create_self(heist_chars_test.self heist_chars_test)
vita_create_vpk(heist_chars_test.vpk HSTCHR001 heist_chars_test.self
  VERSION 01.00
  NAME "Test Personnages"
)

# Test avec le vrai Fabio (donnees dans ux0:data/heist/)
add_executable(heist_fabio_test fab_main.c)
target_link_libraries(heist_fabio_test ${LIBS})
vita_create_self(heist_fabio_test.self heist_fabio_test)
vita_create_vpk(heist_fabio_test.vpk HSTFAB001 heist_fabio_test.self
  VERSION 01.00
  NAME "Test Fabio"
)

# Test multi-personnages (8 vrais persos, texture partagee)
add_executable(heist_multi_test multi_main.c)
target_link_libraries(heist_multi_test ${LIBS})
vita_create_self(heist_multi_test.self heist_multi_test)
vita_create_vpk(heist_multi_test.vpk HSTMUL001 heist_multi_test.self
  VERSION 01.00
  NAME "Test Multi Persos"
)

# Test niveau reel (tutorial) + personnages
add_executable(heist_level_test level_main.c)
target_link_libraries(heist_level_test ${LIBS})
vita_create_self(heist_level_test.self heist_level_test)
vita_create_vpk(heist_level_test.vpk HSTLVL001 heist_level_test.self
  VERSION 01.00
  NAME "Test Niveau"
)

# Prototype jouable : Daisy dans le niveau tutorial (collisions, ennemis aux vraies positions)
add_executable(heist_play_test play_main.c)
target_link_libraries(heist_play_test ${LIBS})
vita_create_self(heist_play_test.self heist_play_test)
vita_create_vpk(heist_play_test.vpk HSTPLY001 heist_play_test.self
  VERSION 01.00
  NAME "Heist 2 Jouable"
)
