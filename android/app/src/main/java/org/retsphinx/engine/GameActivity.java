package org.retsphinx.engine;

import org.libsdl.app.SDLActivity;

/**
 * Minimal SDL3 activity subclass.  SDL3's SDLActivity handles all JNI
 * plumbing; we only need to declare which native library to load.
 * The CMake build produces libmain.so (OUTPUT_NAME "main").
 */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
