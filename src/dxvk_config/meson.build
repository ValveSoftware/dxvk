dxvk_config_res = wrc_generator.process('version.rc')

dxvk_config_src = [
  'dxvk_config.cpp',
]

dxvk_config_dll = shared_library('dxvk_config'+dll_ext, dxvk_config_src, dxvk_config_res,
  name_prefix         : '',
  dependencies        : [ lib_dxgi, dxvk_dep ],
  include_directories : dxvk_include_path,
  install             : true,
  objects             : not dxvk_msvc ? 'dxvk_config'+def_spec_ext : [],
  vs_module_defs      : 'dxvk_config'+def_spec_ext,
  override_options    : ['cpp_std='+dxvk_cpp_std])

install_headers('dxvk_config.h',
  subdir              : 'dxvk')

dxvk_config_dep = declare_dependency(
  link_with           : [ dxvk_config_dll ],
  include_directories : [ dxvk_include_path ])
