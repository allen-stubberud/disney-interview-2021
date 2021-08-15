cmake_minimum_required(VERSION 3.0)

foreach(REQ IDENTIFIER INPUT_FILE OUTPUT_FILE)
  if (NOT ${REQ})
    message(FATAL_ERROR "Required variable is missing: ${REQ}")
  endif ()
endforeach()

file(READ "${INPUT_FILE}" DATA HEX)
string(REGEX REPLACE "(..)" "0x\\1,\n" DATA "${DATA}")

file(
  WRITE "${OUTPUT_FILE}"
  "
#include <cstddef>
extern const unsigned char ${IDENTIFIER}_data[] = {
${DATA}
};
extern const size_t ${IDENTIFIER}_size = sizeof(${IDENTIFIER}_data);
  "
  )
