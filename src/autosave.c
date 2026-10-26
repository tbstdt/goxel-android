/* Goxel 3D voxels editor
 *
 * copyright (c) 2015-2022 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Autosave, for platforms that can kill the app at any time (android).
 *
 * We keep a copy of the unsaved image in a private directory: when the app
 * goes to the background, and periodically.  On start, if there is one, we
 * propose to restore it.  The path of the original file is stored next to
 * it, so that saving after a restore goes to the right file.
 */

#include "goxel.h"

#include <errno.h>
#include <unistd.h>

#define AUTOSAVE_PERIOD 60

static struct {
    char        dir[1024];
    double      time;   // Time of the last check.
    uint32_t    key;    // Image key of the last autosave, 0 if none.
} g = {};

static void get_path(char *buf, size_t size, const char *ext)
{
    snprintf(buf, size, "%s/autosave.%s", g.dir, ext);
}

void autosave_init(const char *dir)
{
    snprintf(g.dir, sizeof(g.dir), "%s", dir);
    g.time = sys_get_time();
}

void autosave_delete(void)
{
    char path[1024];
    if (!g.dir[0]) return;
    get_path(path, sizeof(path), "gox");
    remove(path);
    get_path(path, sizeof(path), "txt");
    remove(path);
    g.key = 0;
}

void autosave(bool force)
{
    char path[1024], tmp[1024];
    uint32_t key;
    FILE *file;

    if (!g.dir[0] || !goxel.image) return;
    if (!force && sys_get_time() - g.time < AUTOSAVE_PERIOD) return;
    g.time = sys_get_time();
    key = image_get_key(goxel.image);
    // Nothing that isn't already saved.
    if (key == goxel.image->saved_key) {
        if (g.key) autosave_delete();
        return;
    }
    if (key == g.key) return;

    get_path(path, sizeof(path), "gox");
    get_path(tmp, sizeof(tmp), "gox.tmp");
    save_to_file(goxel.image, tmp);
    if (rename(tmp, path) != 0) {
        LOG_E("Cannot write autosave: %s", strerror(errno));
        return;
    }
    get_path(path, sizeof(path), "txt");
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s", goxel.image->path ?: "");
        fclose(file);
    }
    g.key = key;
    LOG_I("Autosaved");
}

static int restore_popup(void *data)
{
    char path[1024], orig[1024] = "";
    FILE *file;
    int ret = 0;

    gui_text(_("The app was closed with unsaved changes."));
    gui_row_begin(0);
    if (gui_button(_("Restore"), 0, 0)) {
        get_path(path, sizeof(path), "txt");
        file = fopen(path, "r");
        if (file) {
            if (!fgets(orig, sizeof(orig), file)) orig[0] = '\0';
            fclose(file);
        }
        get_path(path, sizeof(path), "gox");
        image_delete(goxel.image);
        goxel.image = image_new();
        load_from_file(path, true);
        // Back to the original file, and still unsaved.
        free(goxel.image->path);
        goxel.image->path = orig[0] ? strdup(orig) : NULL;
        goxel.image->saved_key = 0;
        ret = 1;
    }
    if (gui_button(_("Discard"), 0, 0)) {
        autosave_delete();
        ret = 2;
    }
    gui_row_end();
    return ret;
}

void autosave_check_restore(void)
{
    char path[1024];
    if (!g.dir[0]) return;
    get_path(path, sizeof(path), "gox");
    if (access(path, F_OK) != 0) return;
    gui_open_popup(_("Restore unsaved work?"), GUI_POPUP_RESIZE, NULL,
                   restore_popup);
}
