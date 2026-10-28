set(target "${SOURCE_DIR}/reproc/src/process.windows.c")
file(READ "${target}" content)
set(original "${content}")
string(FIND "${content}" "CREATE_NO_WINDOW" found)
if(found EQUAL -1)
  string(REPLACE "CREATE_NEW_PROCESS_GROUP |"
    "CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW |" content "${content}")
endif()
string(REPLACE ".dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW,"
  ".dwFlags = STARTF_USESTDHANDLES," content "${content}")
if(NOT content STREQUAL original)
  file(WRITE "${target}" "${content}")
endif()
