cmake_minimum_required(VERSION 3.16)
project(SolarCalculator CXX)

set(CMAKE_CXX_STANDARD 20)

# Включаем поддержку файлов ресурсов для Windows
if(WIN32)
    set(PROJECT_SOURCES
        main.cpp
        resources.rc  # Компилятор автоматически превратит этот файл в объектный и вшьет в exe
    )
else()
    set(PROJECT_SOURCES
        main.cpp
    )
endif()

add_executable(SolarCalc ${PROJECT_SOURCES})
target_link_libraries(SolarCalc raylib)