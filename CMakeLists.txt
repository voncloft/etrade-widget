cmake_minimum_required(VERSION 3.20)
project(etradeplasmoid VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/test" CACHE PATH "Install prefix" FORCE)
endif()

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network)
find_package(KF6 REQUIRED COMPONENTS I18n CoreAddons)

add_subdirectory(plugin)

install(DIRECTORY package/
        DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/org.von.etrade
        PATTERN ".git" EXCLUDE)
