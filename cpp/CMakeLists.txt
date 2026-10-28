cmake_minimum_required(VERSION 3.10)

# Выбираем кросс-компиляторы GCC из переменной окружения GCC_COMPILER.
set(CMAKE_C_COMPILER "$ENV{GCC_COMPILER}-gcc")
set(CMAKE_CXX_COMPILER "$ENV{GCC_COMPILER}-g++")

# Имя проекта; оно используется, в частности, как имя исполняемого файла.
project(HelloYolov8)

# Собираем вариант для Rockchip RV1106/RV1103.
add_definitions(-DRV1106_1103)

# Подключаем сборку сторонней библиотеки RKNN и вспомогательных утилит проекта.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

# При запуске искать динамические библиотеки в каталоге lib рядом с программой.
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# Собираем список C++-файлов реализации из каталога src.
file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)

# Ищем установленную в проекте OpenCV 4 и добавляем её заголовки.
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS} )

# Находим системную библиотеку потоков, используемую приложением/зависимостями.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Создаём исполняемый файл из исходников, найденных выше.
add_executable(${PROJECT_NAME}
    ${SRCS}
)

# Связываем приложение с RKNN, OpenCV, потоками и утилитами проекта.
target_link_libraries(${PROJECT_NAME}
    ${LIBRKNNRT}
    ${OpenCV_LIBS}
    Threads::Threads
    imageutils
    fileutils
)

# Пути к заголовкам, нужным при компиляции исходников приложения.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    ${OpenCV_INCLUDE_DIRS}
)

# Устанавливаем программу и ресурсы в cpp/bin для запуска на целевом устройстве.
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/bin")
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})

# Копируем список названий классов и все RKNN-модели из корневого каталога model.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/labels.txt DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
