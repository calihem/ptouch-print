# Utilize gettext multilingual internationalization services
if(Gettext_FOUND)
	add_custom_target(
		pot-update
		ALL
		DEPENDS ${CMAKE_SOURCE_DIR}/po/ptouch.pot
	)
	add_dependencies(pot-update git-version)

	# generate pot files using xgettext
	if(GETTEXT_XGETTEXT_EXECUTABLE)
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
	else()
		message(DEBUG "Variable GETTEXT_XGETTEXT_EXECUTABLE not set")
	endif()

	# read available languages from LINGUAS file while ignoring comments
	file(STRINGS po/LINGUAS LINGUAS REGEX "^[^#]")

	# merge po files
	if(GETTEXT_MSGMERGE_EXECUTABLE)
		add_custom_target(
			po-merge
			ALL
			DEPENDS ${CMAKE_SOURCE_DIR}/po/ptouch.pot
		)
		add_dependencies(po-merge pot-update)

		foreach(LINGUA IN LISTS LINGUAS)
			add_custom_command(
				TARGET po-merge
				PRE_BUILD
				COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE}
					--update
					--quiet
					${CMAKE_SOURCE_DIR}/po/${LINGUA}.po
					${CMAKE_SOURCE_DIR}/po/ptouch.pot
			)
		endforeach()
	endif()

	# compile po files
	foreach(LINGUA IN LISTS LINGUAS)
		gettext_process_po_files(
			${LINGUA}
			ALL
			PO_FILES ${CMAKE_SOURCE_DIR}/po/${LINGUA}.po
		)
	endforeach()

	# install mo files
	foreach(LINGUA IN LISTS LINGUAS)
		install(
			FILES "${CMAKE_CURRENT_BINARY_DIR}/${LINGUA}.gmo"
			DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${LINGUA}/LC_MESSAGES"
			RENAME "${PROJECT_NAME}.mo"
		)
	endforeach()
endif()
