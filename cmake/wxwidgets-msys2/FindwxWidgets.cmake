# Compatibility bridge for PCem's legacy FindwxWidgets variables.
# MSYS2's wxWidgets package exports imported CMake targets instead.

find_package(wxWidgets CONFIG REQUIRED COMPONENTS ${wxWidgets_FIND_COMPONENTS})

if(NOT wxWidgets_FOUND)
    set(wxWidgets_FOUND FALSE)
    return()
endif()

# The config package already provides wx:: targets and the compatibility
# wxWidgets_LIBRARIES list. Keep PCem's legacy include() call valid.
set(wxWidgets_USE_FILE "${CMAKE_CURRENT_LIST_DIR}/wxWidgetsUseFile.cmake")

get_filename_component(_wxwidgets_prefix "${wxWidgets_DIR}/../../.." ABSOLUTE)
find_program(wxWidgets_wxrc_EXECUTABLE
    NAMES wxrc.exe wxrc-3.2.exe wxrc
    HINTS "${_wxwidgets_prefix}/bin"
    REQUIRED
)

mark_as_advanced(wxWidgets_wxrc_EXECUTABLE)
