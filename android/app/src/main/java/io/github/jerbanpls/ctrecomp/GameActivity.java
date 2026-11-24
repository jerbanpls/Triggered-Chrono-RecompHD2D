// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.jerbanpls.ctrecomp;

import java.io.File;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity (from the SDL 2.32.10 tarball) running libmain.so, the
 * engine and frontend, on the ROM and module ImportActivity copied in.
 */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        File dir = getFilesDir();
        boolean hd2d = getSharedPreferences(ImportActivity.PREFS, MODE_PRIVATE)
                .getBoolean(ImportActivity.PREF_HD2D, false);
        String rom = new File(dir, ImportActivity.ROM).getAbsolutePath();
        String module = new File(dir, ImportActivity.MODULE).getAbsolutePath();
        return hd2d ? new String[] {"--rom", rom, "--module", module, "--hd2d"}
                    : new String[] {"--rom", rom, "--module", module};
    }
}
