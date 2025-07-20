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

# Configure gettext
if(Gettext_FOUND)
	# generate pot files using xgettext
	find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)
	if(GETTEXT_XGETTEXT_EXECUTABLE)
		add_custom_target(
			pot-update
			ALL
			DEPENDS ${CMAKE_SOURCE_DIR}/po/ptouch.pot
		)
		add_dependencies(pot-update git-version)

		file(GLOB_RECURSE C_FILES RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/*.c)
		file(STRINGS ${CMAKE_BINARY_DIR}/version.h VERSION_LINE REGEX "VERSION")
		string(REGEX MATCH "\".*\"$" PVERSION ${VERSION_LINE})
		add_custom_command(
			TARGET pot-update
			PRE_BUILD
			COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE}
				--keyword=_
				--keyword=N_
				--force-po
				--package-name=${PROJECT_NAME}
				--package-version=${PVERSION}
				--copyright-holder="Dominic Radermacher <dominic@familie-radermacher.ch>"
				--msgid-bugs-address="dominic@familie-radermacher.ch"
				--output ${CMAKE_SOURCE_DIR}/po/ptouch.pot
				${C_FILES}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		)
	endif()

	# merge po files
	find_program(GETTEXT_MSGMERGE_EXECUTABLE, msgmerge)
	if(GETTEXT_MSGMERGE_EXECUTABLE)
		add_custom_target(
			po-merge
			ALL
			DEPENDS ${CMAKE_SOURCE_DIR}/po/ptouch.pot
		)
		add_dependencies(po-merge pot-update)

		file(GLOB PO_FILES ${CMAKE_SOURCE_DIR}/po/*.po)
		foreach(PO_FILE IN ITEMS ${PO_FILES})
			add_custom_command(
				TARGET po-merge
				PRE_BUILD
				COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE}
					--update
					--quiet
					${PO_FILE}
					${CMAKE_SOURCE_DIR}/po/ptouch.pot
			)
		endforeach()
	endif()

	# read available languages from LINGUAS file while ignoring comments
	file(STRINGS po/LINGUAS LINGUAS REGEX "^[^#]")

	foreach(language IN LISTS LINGUAS)
		gettext_process_po_files(
			${language}
			ALL
			PO_FILES ${CMAKE_SOURCE_DIR}/po/${language}.po
		)
	endforeach()

	foreach(language IN LISTS LINGUAS)
		install(
			FILES "${CMAKE_CURRENT_BINARY_DIR}/${language}.gmo"
			DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${language}/LC_MESSAGES"
			RENAME "${PROJECT_NAME}.mo"
		)
	endforeach()
endif()

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
	install(FILES udev/20-usb-ptouch-permissions.rules DESTINATION /etc/udev/rules.d)
	install(CODE "execute_process(COMMAND udevadm control --reload-rules)")
endif()
