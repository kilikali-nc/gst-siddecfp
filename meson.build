project('gst-template', 'c', version : '1.19.0.1', license : 'LGPL')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

have_cxx = add_languages('cpp', native: false, required: false)

cc = meson.get_compiler('c')
if have_cxx
  cxx = meson.get_compiler('cpp')
endif

gst_version = meson.project_version()

api_version = '1.0'

configinc = include_directories('.')

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif


gst_dep = dependency('gstreamer-1.0', version : '>=1.18',
    required : true, fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : '>=1.18',
  fallback : ['gstreamer', 'gst_base_dep'])

subdir('gst-app')
subdir('gst-plugin')
