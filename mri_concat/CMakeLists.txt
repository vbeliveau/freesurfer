project(mri_concat)

include_directories(${FS_INCLUDE_DIRS})

add_executable(mri_concat mri_concat.c)
target_link_libraries(mri_concat utils)

add_test_script(NAME mri_concat_test SCRIPT test.py DEPENDS mri_concat)

install(TARGETS mri_concat DESTINATION bin)
