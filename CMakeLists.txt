cmake_minimum_required(VERSION 3.16)
project(FPV_Hunter_Pro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ============================================================
# СТАТИЧЕСКАЯ СБОРКА (КРОМЕ LIBIIO)
# ============================================================

if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread")
endif()

# ============================================================
# ПОИСК Qt5 (динамическая, но статически линкуется)
# ============================================================

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIIO REQUIRED libiio)

# ============================================================
# СОБИРАЕМ EXE
# ============================================================

add_executable(FPV_Hunter_Pro WIN32 fpv_hunter_final.cpp)

# Qt5 линкуем статически (если есть статические библиотеки)
# В MSYS2 они обычно есть
target_link_libraries(FPV_Hunter_Pro Qt5::Core Qt5::Widgets)

# libiio линкуем динамически
if(LIBIIO_FOUND)
    target_link_libraries(FPV_Hunter_Pro ${LIBIIO_LIBRARIES})
endif()

target_include_directories(FPV_Hunter_Pro PRIVATE ${LIBIIO_INCLUDE_DIRS})

if(WIN32)
    set_target_properties(FPV_Hunter_Pro PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
