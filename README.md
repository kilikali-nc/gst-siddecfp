# GStreamer sidecfp plugin

SidDecFP uses sidplayfp library version 2.x to play C64 SID audio files. It
also fixes some problems original SidDec-plugin had.

It misght have some features that are not wanted to GStreamer, like uploading
ROM files using GByteArray (as property) or too hight priority.

There are two kinds of SID-files. PSID and RSID. at least RSIDs¬ are straight
playable with real hardware.

I think GStreames has bug. It regognized only x-sid type files which are really
PSID-files. It should bw x-psid and then x-rsid type files (RSID-files) are
ignored.

See gstreamer.patch possibly to get RSIDs to work. Patch is not tested, because
I lost original pach and had no time to test it.

gst-app make typefind hack to find right decoder without GStreamers support.
¬
Some RSIDs requires ROM files. Like Wally Bebens Tetris.sid¬ requires
just kernal.bin, kernal-906145-02.bin works fine with it. See links to ¬
¬
Seems to be so that PSIDs does not require ROM files.¬
¬
* Links:¬
¬
Kernal rom bin: https://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/kernal.906145-02.bin
- try first without and then place to workingdir and rename to¬ kernal.bin
¬
RSID song: https://hvsc.brona.dk/HVSC/C64Music/MUSICIANS/B/Beben_Wally/Tetris.sid
- RSID file, which works only with new plugin when using sidfpdec¬ with kernal.bin
¬
PSID song: https://hvsc.brona.dk/HVSC/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid
- PSID file, which works with original siddec decoder and¬without kernal.bin

* gst-app :
  Special siddecfp version of meson-based layout for writing a GStreamer-based
  application which supports kernal, basic and chargen roms.

* gst-plugin :
  siddecfp meson-based GStreamer plug-in.

## License

Plugin is GPL2 as sidplayfp-library. It will be ugly-plugin as original
siddec 

This code is provided under a MIT license [MIT], which basically means "do
with it as you wish, but don't blame us if it doesn't work". You can use
this code for any project as you wish, under any license as you wish. We
recommend the use of the LGPL [LGPL] license for applications and plugins,
given the minefield of patents the multimedia is nowadays. See our website
for details [Licensing].

## Usage

Configure and build all examples (application and plugins) as such:

    meson builddir
    ninja -C builddir

See <https://mesonbuild.com/Quick-guide.html> on how to install the Meson
build system and ninja.

Modify `gst-plugin/meson.build` to add or remove source files to build or
add additional dependencies or compiler flags or change the name of the
plugin file to be installed.

Modify `meson.build` to check for additional library dependencies
or other features needed by your plugin.

Once the plugin is built you can either install system-wide it with `sudo ninja
-C builddir install` (however, this will by default go into the `/usr/local`
prefix where it won't be picked up by a `GStreamer` installed from packages, so
you would need to set the `GST_PLUGIN_PATH` environment variable to include or
point to `/usr/local/lib/gstreamer-1.0/` for your plugin to be found by a
from-package `GStreamer`).

Alternatively, you will find your plugin binary in `builddir/gst-plugins/src/`
as `libgstplugin.so` or similar (the extension may vary), so you can also set
the `GST_PLUGIN_PATH` environment variable to the `builddir/gst-plugins/src/`
directory (best to specify an absolute path though).

You can also check if it has been built correctly with:

    gst-inspect-1.0 builddir/gst-plugins/src/libgstplugin.so

## Auto-generating your own plugin

You will find a helper script in `gst-plugins/tools/make_element` to generate
the source/header files for a new plugin.

To create sources for `myfilter` based on the `gsttransform` template run:

``` shell
cd src;
../tools/make_element myfilter gsttransform
```

This will create `gstmyfilter.c` and `gstmyfilter.h`. Open them in an editor and
start editing. There are several occurances of the string `template`, update
those with real values. The plugin will be called `myfilter` and it will have
one element called `myfilter` too. Also look for `FIXME:` markers that point you
to places where you need to edit the code.

You can then add your sources files to `gst-plugins/meson.build` and re-run
ninja to have your plugin built.


[MIT]: http://www.opensource.org/licenses/mit-license.php or COPYING.MIT
[LGPL]: http://www.opensource.org/licenses/lgpl-license.php or COPYING.LIB
[Licensing]: https://gstreamer.freedesktop.org/documentation/application-development/appendix/licensing.html
