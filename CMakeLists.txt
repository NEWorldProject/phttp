cmake_minimum_required(VERSION 3.11)

project(KLSXX)

kls_add_library_module(kls.phttp klsxx::phttp)
kls_public_source_directory(kls.phttp Published)
kls_module_source_directory(kls.phttp Module)
target_link_libraries(kls.phttp PUBLIC kls.essential kls.io)
