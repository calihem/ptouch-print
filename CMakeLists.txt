cmake_minimum_required(VERSION 3.15)

project(ptouch-print C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(GNUInstallDirs)
set(CMAKE_INSTALL_PREFIX /usr)
set(CMAKE_C_STANDARD 11)

# Configure required dependencies
find_package(Gettext REQUIRED)
find_package(GD REQUIRED)
find_package(Git REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Intl REQUIRED)

pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Configure project executable
add_executable(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_BINARY_DIR}	# HB9HEI - location of generated version.h
	${CMAKE_SOURCE_DIR}/include
	${GD_INCLUDE_DIR}
	${LIBUSB_INCLUDE_DIRS}
	${Intl_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	${GD_LIBRARIES}
	${LIBUSB_LIBRARIES}
	${LIBUSB_LINK_LIBRARIES}
	${Intl_LIBRARIES}
)

target_sources(${PROJECT_NAME} PRIVATE
	include/ptouch.h
	include/gettext.h
	src/libptouch.c
	src/ptouch-print.c
)

add_dependencies(${PROJECT_NAME}
	git-version
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
	LOCALEDIR="${CMAKE_INSTALL_LOCALEDIR}"
	USING_CMAKE=1
	PACKAGE="ptouch-print"
)

target_compile_options(${PROJECT_NAME} PUBLIC
	-g
	-Wall
	-Wextra
	-Wunused
	-O3
	-fPIC
)

# HB9HEI - custom target that produces version.h	(req. cmake 3.0)
add_custom_target(git-version ALL
	${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gitversion.cmake
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ptouch-print.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(EXISTS /etc/udev/rules.d)
	install(FILES udev/90-usb-ptouch-permissions.rules DESTINATION /etc/udev/rules.d)
	install(CODE "execute_process(COMMAND udevadm control --reload-rules)")
endif()
