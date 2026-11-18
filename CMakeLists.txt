cmake_minimum_required(VERSION 3.16)
project(Sonificacion CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Para que los acentos de los textos se vean bien
if(MSVC)
  add_compile_options(/utf-8)
endif()

# MFC (Microsoft Foundation Classes) como biblioteca compartida (DLL)
# Debe ir antes de add_executable
set(CMAKE_MFC_FLAG 2)
add_definitions(-D_AFXDLL -DUNICODE -D_UNICODE)

# Busca OpenCV en C:/opencv (carpeta de extraccion)
file(GLOB OPENCV_CFG_DIRS "C:/opencv/build/x64/vc*/lib")
if(NOT OPENCV_CFG_DIRS)
  message(FATAL_ERROR "No encontré OpenCV en C:/opencv/build/x64/. Revisa la ruta de extracción.")
endif()

list(SORT OPENCV_CFG_DIRS)
list(GET OPENCV_CFG_DIRS -1 OpenCV_DIR)

find_package(OpenCV REQUIRED)

# Motor.cpp = OpenCV + audio (sin MFC)   |   Ventana.cpp = interfaz MFC (sin OpenCV)
add_executable(Sonificacion WIN32 Motor.cpp Motor.h Ventana.cpp)
target_include_directories(Sonificacion PRIVATE ${OpenCV_INCLUDE_DIRS})
# winmm = reproducir el audio (PlaySound)
target_link_libraries(Sonificacion PRIVATE ${OpenCV_LIBS} winmm)
# Nota: no hace falta /ENTRY. Ventana.cpp define WinMain y le pasa el control a MFC (AfxWinMain).