add_executable(Test.Common.TabletProfileConfigAdaptor testtabletprofileconfigadaptor.cpp ../commontestutils.cpp)
add_test(NAME Test.Common.TabletProfileConfigAdaptor COMMAND Test.Common.TabletProfileConfigAdaptor)
ecm_mark_as_test(Test.Common.TabletProfileConfigAdaptor)
target_link_libraries(Test.Common.TabletProfileConfigAdaptor ${WACOM_COMMON_TEST_LIBS})
