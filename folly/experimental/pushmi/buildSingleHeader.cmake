
function(BuildSingleHeader HeaderName)
    set(header_files ${ARGN})
    # write out single-header
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/${HeaderName}-single-header.h" header)
    string(APPEND incls "${header}")

    foreach(f ${header_files})
        message("processing ${f}")
        file(READ ${f} contents)
        string(REGEX REPLACE "(([\t ]*#[\t ]*pragma[\t ]+once)|([\t ]*#[\t ]*include))" "//\\1" filtered "${contents}")
        string(APPEND incls "${filtered}")
    endforeach()

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/${HeaderName}-single-footer.h" footer)
    string(APPEND incls "${footer}")

    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/include/${HeaderName}.h "${incls}")
endfunction()

set(header_files 
    # keep in inclusion order

    "${CMAKE_CURRENT_SOURCE_DIR}/external/meta/include/meta/meta_fwd.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/meta/include/meta/meta.hpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/detail/if_constexpr.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/detail/concept_def.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/traits.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/detail/functional.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/detail/opt.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/forwards.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/extension_points.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/properties.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/concepts.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/boosters.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/piping.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/none.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/deferred.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/single.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/single_deferred.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/time_single_deferred.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/executor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/flow_single.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/flow_single_deferred.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/trampoline.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/new_thread.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/empty.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/extension_operators.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/just.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/on.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/submit.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/tap.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/transform.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pushmi/o/via.h"
)

BuildSingleHeader("pushmi" ${header_files})