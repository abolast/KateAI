add_executable(themecheck main.cpp ../../src/theme.cpp ../../src/toolcallwidget.cpp ../../src/kateai.qrc)
target_include_directories(themecheck PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(themecheck PRIVATE Qt6::Widgets KF6::ConfigCore KF6::I18n)
set_target_properties(themecheck PROPERTIES AUTOMOC ON)
