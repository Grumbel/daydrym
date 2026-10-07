package com.daydrym;

import org.libsdl.app.SDLActivity;

public class DaydrymActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
