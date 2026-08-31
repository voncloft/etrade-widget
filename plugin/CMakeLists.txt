add_library(etrade_qmlplugin SHARED
  plugin.cpp
  etradeclient.cpp
)

target_include_directories(etrade_qmlplugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(etrade_qmlplugin
  Qt6::Core
  Qt6::Qml
  Qt6::Network
  KF6::I18n
  KF6::CoreAddons
)

set_target_properties(etrade_qmlplugin PROPERTIES
  OUTPUT_NAME "etrade"
)

install(TARGETS etrade_qmlplugin
  LIBRARY DESTINATION ${KDE_INSTALL_QMLDIR}/org/von/etrade
)

install(FILES
  etrade_plugin.json
  qmldir
  DESTINATION ${KDE_INSTALL_QMLDIR}/org/von/etrade
)
