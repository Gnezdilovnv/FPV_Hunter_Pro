cmake_minimum_required(VERSION 3.16)
project(FPV_Hunter_Pro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ============================================================
# СТАТИЧЕСКАЯ СБОРКА (КРОМЕ LIBIIO)
# ============================================================

if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# ============================================================
# ПОИСК Qt5 (статическая версия в MSYS2 есть)
# ============================================================

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

# ============================================================
# LIBIIO - ТОЛЬКО ДИНАМИЧЕСКАЯ (через -l)
# ============================================================

# Просто указываем линковщику libiio (динамическую)
set(LIBIIO_LIBRARIES -liio)

# ============================================================
# СОБИРАЕМ EXE
# ============================================================

add_executable(FPV_Hunter_Pro WIN32 fpv_hunter_final.cpp)

target_link_libraries(FPV_Hunter_Pro 
    Qt5::Core 
    Qt5::Widgets 
    ${LIBIIO_LIBRARIES}
)

if(WIN32)
    set_target_properties(FPV_Hunter_Pro PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
