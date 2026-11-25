// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.jerbanpls.ctrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Imports the user's ROM image and a prebuilt game module through the
 * Storage Access Framework (ACTION_OPEN_DOCUMENT: no storage permission),
 * copying both into the app's files dir as rom.sfc and ct_game.so. The
 * module is made read-only, as Android 14+ requires of dynamically loaded
 * code. When both are there, Play starts GameActivity.
 */
public class ImportActivity extends Activity {
    static final String ROM = "rom.sfc";
    static final String MODULE = "ct_game.so";
    static final String PREFS = "ct";
    static final String PREF_HD2D = "hd2d";

    private static final int REQ_ROM = 1;
    private static final int REQ_MODULE = 2;

    private TextView status;
    private Button play;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);

        TextView intro = new TextView(this);
        intro.setText(R.string.intro);
        col.addView(intro);

        Button rom = new Button(this);
        rom.setText(R.string.pick_rom);
        rom.setOnClickListener(v -> pick(REQ_ROM));
        col.addView(rom);

        Button module = new Button(this);
        module.setText(R.string.pick_module);
        module.setOnClickListener(v -> pick(REQ_MODULE));
        col.addView(module);

        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        CheckBox hd2d = new CheckBox(this);
        hd2d.setText(R.string.hd2d);
        hd2d.setChecked(prefs.getBoolean(PREF_HD2D, false));
        hd2d.setOnCheckedChangeListener((b, on) -> prefs.edit().putBoolean(PREF_HD2D, on).apply());
        col.addView(hd2d);

        status = new TextView(this);
        status.setPadding(0, pad, 0, pad);
        col.addView(status);

        play = new Button(this);
        play.setText(R.string.play);
        play.setOnClickListener(v -> {
            startActivity(new Intent(this, GameActivity.class));
        });
        col.addView(play);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(col, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        refresh(null);
    }

    private void pick(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");   // .sfc and .so have no registered MIME type
        startActivityForResult(i, req);
    }

    private String describe(File f) {
        return f.isFile() ? getString(R.string.present, (int) f.length()) : getString(R.string.missing);
    }

    private void refresh(String error) {
        File rom = new File(getFilesDir(), ROM), module = new File(getFilesDir(), MODULE);
        String s = getString(R.string.rom_label, describe(rom)) + "\n"
                + getString(R.string.module_label, describe(module));
        if (error != null)
            s += "\n\n" + error;
        status.setText(s);
        play.setEnabled(rom.isFile() && module.isFile());
    }

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null)
            return;
        String error = null;
        try {
            if (req == REQ_ROM)
                copyIn(data.getData(), ROM, false);
            else if (req == REQ_MODULE)
                copyIn(data.getData(), MODULE, true);
        } catch (IOException e) {
            error = getString(R.string.copy_failed, e.getMessage());
        }
        refresh(error);
    }

    /** True if head starts an ELF64 little-endian shared object for AArch64. */
    private static boolean isArm64So(byte[] h, int n) {
        return n >= 20 && h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F'
                && h[4] == 2 && h[5] == 1          // 64-bit, little-endian
                && h[16] == 3 && h[17] == 0        // e_type ET_DYN
                && (h[18] & 0xff) == 0xB7 && h[19] == 0;   // e_machine EM_AARCH64
    }

    /** Copy uri to files/name through a temporary file; module: check it, then read-only. */
    private void copyIn(Uri uri, String name, boolean module) throws IOException {
        File dir = getFilesDir();
        File tmp = new File(dir, name + ".part"), dst = new File(dir, name);
        byte[] buf = new byte[1 << 16];
        boolean checked = !module;
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(tmp)) {
            if (in == null)
                throw new IOException("cannot open " + uri);
            int n;
            while ((n = in.read(buf)) > 0) {
                if (!checked) {
                    if (!isArm64So(buf, n))
                        throw new IOException(getString(R.string.not_module));
                    checked = true;
                }
                out.write(buf, 0, n);
            }
        } catch (IOException e) {
            tmp.delete();
            throw e;
        }
        // A read-only file can still be replaced: the directory is writable.
        if (dst.exists() && !dst.delete())
            throw new IOException("cannot replace " + dst);
        if (!tmp.renameTo(dst))
            throw new IOException("cannot rename to " + dst);
        if (module && !dst.setReadOnly())
            throw new IOException("cannot make " + dst + " read-only");
    }
}
