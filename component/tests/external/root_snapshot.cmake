# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# What an unset path would write at a filesystem root (/awva_components, D:\awva_va_component, ...):
# every file and folder under the roots' awva_* and external_va* entries, with its time. Taken
# before and after a run, the difference is what the run wrote there; what an older build left is
# in both and doesn't count.
#   awva_root_snapshot(<var> <root>...)                 roots end with a slash ("/", "D:/")
#   awva_root_written(<var> <before> <after>)           the entries new or changed in <after>
cmake_policy(SET CMP0057 NEW) # if(IN_LIST) (also when included by a script, cmake -P)

function(awva_root_snapshot var)
  set(entries "")
  foreach(root IN LISTS ARGN)
    file(GLOB tops LIST_DIRECTORIES true "${root}awva_*" "${root}external_va*")
    foreach(top IN LISTS tops)
      set(paths "${top}")
      if(IS_DIRECTORY "${top}")
        file(GLOB_RECURSE inner LIST_DIRECTORIES true "${top}/*")
        list(APPEND paths ${inner})
      endif()
      foreach(p IN LISTS paths)
        file(TIMESTAMP "${p}" t "%Y%m%d%H%M%S" UTC)
        list(APPEND entries "${p}@${t}")
      endforeach()
    endforeach()
  endforeach()
  set(${var} "${entries}" PARENT_SCOPE)
endfunction()

function(awva_root_written var before after)
  set(written "")
  foreach(e IN LISTS after)
    if(NOT e IN_LIST before)
      string(REGEX REPLACE "@[0-9]*$" "" p "${e}")
      list(APPEND written "${p}")
    endif()
  endforeach()
  set(${var} "${written}" PARENT_SCOPE)
endfunction()
