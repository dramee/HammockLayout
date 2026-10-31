# Внешние зависимости: pugixml (разбор/запись XML) и GoogleTest (тесты).
# Сначала ищется установленная в системе версия, при её отсутствии
# исходники загружаются через FetchContent с проверкой контрольной суммы.

include(FetchContent)

find_package(pugixml 1.11 QUIET CONFIG)
if(NOT TARGET pugixml::pugixml)
    message(STATUS "pugixml не найден в системе, загружаем v1.14")
    FetchContent_Declare(pugixml
        URL https://github.com/zeux/pugixml/archive/refs/tags/v1.14.tar.gz
        URL_HASH SHA256=610f98375424b5614754a6f34a491adbddaaec074e9044577d965160ec103d2e
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(pugixml)
endif()

if(HAMMOCK_BUILD_TESTS)
    find_package(GTest 1.12 QUIET CONFIG)
    if(NOT TARGET GTest::gtest_main)
        message(STATUS "GoogleTest не найден в системе, загружаем v1.15.2")
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
            DOWNLOAD_EXTRACT_TIMESTAMP ON
            EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()
