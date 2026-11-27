# Run in the Dear ImGui sources after download. A table whose stretched columns share its width by their contents
# (ImGuiTableFlags_SizingStretchProp) divides zero by zero in its first frame: a new table keeps its columns' content
# widths at 0 for that frame (TableBeginInitMemory()'s IsPreserveWidthAuto), and each weight is a width over their sum.
# The NaN weight reaches the width the table gives its window (EndTable()'s ColumnsAutoFitWidth, for a stretched column
# that cannot be resized: every column of a table without ImGuiTableFlags_Resizable), and the window's next Begin()
# converts it to an integer (CalcWindowContentSizes(), ImTrunc64()): undefined behaviour, which the sanitizers stop at.
# Patched: the weight of a column of a table sized otherwise, 1, while no stretched column has a width yet; from the
# next frame on, the weights are the contents' widths, as before. ImGui's master branch still divides so (2026-11-27,
# 1.93.2 WIP).
#
# FetchContent runs a patch again on the sources already there, when its command changes (the command has this
# script's hash, CMakeLists.txt): the file is patched from the original kept beside it (.fcg-orig), never twice. The
# configuration fails if ImGui's code is no longer the one expected.
set(file imgui_tables.cpp)
if(EXISTS ${file}.fcg-orig)
    configure_file(${file}.fcg-orig ${file} COPYONLY)
else()
    configure_file(${file} ${file}.fcg-orig COPYONLY)
endif()

set(original "column->StretchWeight = (column->WidthAuto / stretch_sum_width_auto) * count_stretch;")
set(patched "column->StretchWeight = stretch_sum_width_auto > 0.0f ? (column->WidthAuto / stretch_sum_width_auto) * \
count_stretch : 1.0f; // RookStudio, cmake/patch_imgui.cmake: no 0 / 0 in a new table's first frame")
file(READ ${file} text)
string(FIND "${text}" "${original}" first)
string(FIND "${text}" "${original}" last REVERSE)
if(first EQUAL -1 OR NOT first EQUAL last)
    message(FATAL_ERROR "patch_imgui.cmake: the code to patch is not where expected in "
                        "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
endif()
string(REPLACE "${original}" "${patched}" text "${text}")
file(WRITE ${file} "${text}")
