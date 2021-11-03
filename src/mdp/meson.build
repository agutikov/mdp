
lib_sources = files('mdp.cc', 'net.cc')


mdp_lib = shared_library('mdp', lib_sources,
                        include_directories : inc,
                        dependencies : external_deps)

mdp_dep = declare_dependency(link_with: mdp_lib, include_directories: inc)
