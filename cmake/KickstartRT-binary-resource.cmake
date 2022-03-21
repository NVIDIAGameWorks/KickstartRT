#
#  Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# Note : There is currently a dependancy issue with this file.
# It is processed at build generation time, so assumes the input resource files exist
# If they don't, then it won't pick them up and the project will need re-configuring once built. 
set(BINARY_RESOURCE_INPUT_FILES "") 
set(BINARY_RESOURCE_EXTERNS "") 
set(BINARY_RESOURCE_CODE_GEN "")

#
# Parses the given resource file and adds it contents to be processed
#
# add_resource_file
#   RC_FILE - windows resource file to be parsed
#
macro(add_resource_file RC_FILE)
    message(STATUS "binary_resource : Processing resource file : "  "${RC_FILE}")

    # Read the file
    file( READ "${RC_FILE}" RC_FILE_CONTENTS )

    # Convert it into a cmake list
    STRING(REGEX REPLACE "\n" ";" RC_FILE_CONTENTS "${RC_FILE_CONTENTS}")
    foreach(LINE ${RC_FILE_CONTENTS})
#        message( "LINE=" "${LINE}" )
 
        # Strip quotes
        string(REGEX REPLACE "\"" "" LINE ${LINE})

        # Ignore comments
        if (${LINE} MATCHES "^#")
            continue()
        endif()

        # output name, type (BINARY), input file
        string(REGEX MATCH "([^\ ]+) ([^\ ]+) ([^\ ]+)" MATCH_RESULT ${LINE})
        
        # We only support 3 matches
        if (${CMAKE_MATCH_COUNT} EQUAL 3) 
            set(RESOURCE_OUTPUT "${CMAKE_MATCH_1}")

#                       message( "MATCH=" "${CMAKE_MATCH_1}" : "${CMAKE_MATCH_2}" : "${CMAKE_MATCH_3}")

            get_filename_component(ABS_FILENAME "${CMAKE_MATCH_3}" ABSOLUTE)
            
            # First match is the input file
            list(APPEND BINARY_RESOURCE_INPUT_FILES "${ABS_FILENAME}")
            
            # Middle match should be BINARY
            if (NOT ${CMAKE_MATCH_2} MATCHES "BINARY" )
                message( FATAL_ERROR "None binary resource file found" )
            endif()

            get_filename_component(RESOURCE_OUTPUT_FILENAME_ONLY ${RESOURCE_OUTPUT} NAME)

             # Last match is the output filename for the virtual filesystem
            string(REPLACE "." "_" RESOURCE_OUTPUT_FILENAME_ONLY ${RESOURCE_OUTPUT_FILENAME_ONLY})

            # ObjCopy generates the appropriate symbols to allow recall of the binary
            # These need externing before use
            list(APPEND BINARY_RESOURCE_EXTERNS "extern char _binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_start[]")
            list(APPEND BINARY_RESOURCE_EXTERNS "extern char _binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_end[]")

            # Generate the LUT
            list(APPEND BINARY_RESOURCE_CODE_GEN "    { \"${RESOURCE_OUTPUT}\", (void*)_binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_start, (void*)_binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_end, (size_t)_binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_end - (size_t)_binary_${RESOURCE_OUTPUT_FILENAME_ONLY}_start },\n")
        else()
            message( FATAL_ERROR "Resource file parsing - didn't find match" )
        endif()
    endforeach()

    # Strip any leading/trailing spaces
    list(TRANSFORM BINARY_RESOURCE_INPUT_FILES STRIP)
endmacro()

#
# Auto generates a header file based on the processed
#
# generate_resource_header
#   HEADER_FILE - header file
#
function(generate_resource_header HEADER_FILE)
    message(STATUS "binary_resource : Autogen : "  "${HEADER_FILE}")

    set(SOURCE "// Auto generated file.\n")
    list(APPEND SOURCE "#pragma once\n")
    list(APPEND SOURCE "\n")
    list(APPEND SOURCE "#include <string.h>\n")
    list(APPEND SOURCE "#include <stdint.h>\n")
    list(APPEND SOURCE "\n")

    # Have to do this to be able to add the semi-colon. It gets stripped if added to the original list
    foreach(extern ${BINARY_RESOURCE_EXTERNS})
        list(APPEND SOURCE ${extern} "\;\n")
    endforeach()
    list(APPEND SOURCE "\n")

    list(APPEND SOURCE "struct ResourceSymbol\n")
    list(APPEND SOURCE "{\n")
    list(APPEND SOURCE "    const char* name\;\n")
    list(APPEND SOURCE "    void* start\;\n")
    list(APPEND SOURCE "    void* end\;\n")
    list(APPEND SOURCE "    size_t size\;\n")
    list(APPEND SOURCE "}\;\n")
    list(APPEND SOURCE "\n")
   
    list(APPEND SOURCE "static const ResourceSymbol g_resource_list[] = {\n")
    list(APPEND SOURCE ${BINARY_RESOURCE_CODE_GEN})
    list(APPEND SOURCE "}\;\n")
    list(APPEND SOURCE "\n")

    list(APPEND SOURCE "const uint32_t g_resource_count = sizeof(g_resource_list)/sizeof(g_resource_list[0])\;\n")
    list(APPEND SOURCE "\n")

    list(APPEND SOURCE "const ResourceSymbol* findResourceSymbol(const char* name)\n")
    list(APPEND SOURCE "{\n")
    list(APPEND SOURCE "    for (uint32_t ii = 0\; ii < g_resource_count\; ii++)\n")
    list(APPEND SOURCE "    {\n")
    list(APPEND SOURCE "        if (!strncmp(name, g_resource_list[ii].name, strlen(name)))\n")
    list(APPEND SOURCE "        {\n")
    list(APPEND SOURCE "            return &g_resource_list[ii]\;\n")
    list(APPEND SOURCE "        }\n")
    list(APPEND SOURCE "    }\n")
    list(APPEND SOURCE "    return nullptr\;\n")
    list(APPEND SOURCE "}\n")
    list(APPEND SOURCE "\n")
    file(WRITE "${HEADER_FILE}" ${SOURCE})
endfunction()

#
# Calls objcopy on all binaries
#
# generate_resource_binaries
#   BINARY_RESOURCE_FILES - in: All of the generated binary files that need linking with the target
#
macro(generate_resource_binaries BINARY_RESOURCE_FILES)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rc)
    foreach(FILENAME ${BINARY_RESOURCE_INPUT_FILES})
        message(STATUS "binary_resource : Binary Copy : "  "${FILENAME}")
        get_filename_component(FILENAME_ONLY ${FILENAME} NAME)
        get_filename_component(FILEPATH_ONLY ${FILENAME} PATH)
        set(OUTPUT_FILENAME ${CMAKE_CURRENT_BINARY_DIR}/rc/${FILENAME_ONLY})
        add_custom_command(OUTPUT ${OUTPUT_FILENAME}.o
            WORKING_DIRECTORY ${FILEPATH_ONLY} 
            DEPENDS ${FILENAME}
            COMMAND ${CMAKE_LINKER} 
            ARGS -r -b binary -o ${OUTPUT_FILENAME}.o ${FILENAME_ONLY})
        list(APPEND ${BINARY_RESOURCE_FILES} ${OUTPUT_FILENAME}.o)
    endforeach(FILENAME)
endmacro()

#
# 1 stop shop - processes all of the resource files, autogenerates the header file and builds the binaries
#
# process_resource_binaries
#   FILES - in: A list of resource files to be processed
#   HEADER - in: The headerfile to be autogenerated
#   BINARIES - out: A list of binaries to link with the target
#
macro(process_resource_binaries FILES HEADER BINARIES)
    foreach(FILE ${FILES})
        add_resource_file(${FILE})
    endforeach()

    generate_resource_header(${HEADER})

    generate_resource_binaries(${BINARIES})
endmacro()