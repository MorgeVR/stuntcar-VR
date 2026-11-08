package com.stuntcarvr;

import org.libsdl.app.SDLActivity;

/**
 * Quest activity: SDL3 provides the EGL context, input (gamepads) and audio; the native
 * code drives OpenXR directly. SDL is linked statically into libmain.so.
 */
public class StuntCarActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "openxr_loader", "main" };
    }
}
