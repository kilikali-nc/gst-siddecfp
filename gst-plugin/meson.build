plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-template-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer template Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

gstaudio_dep = dependency('gstreamer-audio-1.0',
    fallback: ['gst-plugins-base', 'audio_dep'])


sidplayfp_option = get_option('sidplayfp')
if sidplayfp_option.disabled()
  subdir_done()
endif

if not add_languages('cpp', native: false, required: sidplayfp_option)
  subdir_done()
endif

if not cxx.has_header('sidplayfp/sidplayfp.h', required: sidplayfp_option)
  subdir_done()
endif

sidplayfp_dep = cxx.find_library('sidplayfp', required: sidplayfp_option)
if not sidplayfp_dep.found()
  subdir_done()
endif

# sidplayfp plugin works with libsidplayfp 2.x.x
sidfp_code = '''#include <sidplayfp/sidplayfp.h>
                void somefunc (void) {
                  sidplayfp fp = sidplayfp();
                }'''
if not cxx.compiles(sidfp_code, dependencies: sidplayfp_dep, name : 'sidplayfp usage')
  if sidplayfp_option.enabled()
    error('sidplayfp headers and libraries were found, but were not usable')
  endif
  subdir_done()
endif

gstsidfp = library('gstsidfp', 'gstsiddecfp.cc',
  cpp_args : plugin_c_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep, sidplayfp_dep],
  install : true,
  install_dir : plugins_install_dir)
pkgconfig.generate(gstsidfp, install_dir : plugins_pkgconfig_install_dir)
