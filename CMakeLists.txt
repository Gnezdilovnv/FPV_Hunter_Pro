cmake_minimum_required(VERSION 3.16)
project(FPV_Hunter_Pro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ============================================================
# ПОЛНАЯ СТАТИЧЕСКАЯ СБОРКА (ВСЁ ВНУТРИ EXE)
# ============================================================

if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread")
endif()

# ============================================================
# ПОИСК Qt5 (СТАТИЧЕСКАЯ ВЕРСИЯ)
# ============================================================

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIIO REQUIRED libiio)

# ============================================================
# СОБИРАЕМ EXE
# ============================================================

add_executable(FPV_Hunter_Pro WIN32 fpv_hunter_final.cpp)
target_link_libraries(FPV_Hunter_Pro Qt5::Core Qt5::Widgets ${LIBIIO_LIBRARIES})
target_include_directories(FPV_Hunter_Pro PRIVATE ${LIBIIO_INCLUDE_DIRS})

if(WIN32)
    set_target_properties(FPV_Hunter_Pro PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
