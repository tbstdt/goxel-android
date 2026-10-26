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
 * Touch UI simulator: runs goxel with the touch UI in an offscreen EGL
 * surface (GLES3, same code path as android), driven by a script of touch
 * events, and saves screenshots to png files.
 *
 * Usage: goxel-touchsim script.txt
 *
 * Script commands (one per line, coordinates in dp, '#' for comments):
 *   size W H SCALE         Screen size in pixels and density scale.
 *   autosave_dir DIR       Enable autosave in DIR (like android).
 *   autosave               Force an autosave (app going to background).
 *   frames N               Render N frames without changing the inputs.
 *   down ID X Y            Finger ID (0-3) touches the screen.
 *   move ID X Y            Finger ID moves.
 *   up ID                  Finger ID is lifted.
 *   tap X Y                down + 3 frames + up + 3 frames.
 *   drag X0 Y0 X1 Y1 N     One finger drag in N steps.
 *   back                   Android back button.
 *   shot PATH              Save the current frame.
 *   log TEXT               Print TEXT.
 */

#include "goxel.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>

static struct {
    EGLDisplay  display;
    EGLSurface  surface;
    EGLContext  context;
    int         w, h;
    float       scale;
    struct {
        float   pos[2];
        bool    down;
    } fingers[4];
    char        autosave_dir[1024];
} g = {.w = 1080, .h = 2400, .scale = 2.625};

static void sim_log(void *user, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static bool egl_init(void)
{
    EGLint nb;
    EGLConfig config;
    const EGLint attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint surf_attrs[] = {EGL_WIDTH, g.w, EGL_HEIGHT, g.h, EGL_NONE};

    g.display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                      NULL, NULL);
    if (!eglInitialize(g.display, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed\n");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(g.display, attrs, &config, 1, &nb) || nb < 1) {
        fprintf(stderr, "No EGL config\n");
        return false;
    }
    g.context = eglCreateContext(g.display, config, EGL_NO_CONTEXT,
                                 ctx_attrs);
    g.surface = eglCreatePbufferSurface(g.display, config, surf_attrs);
    if (g.context == EGL_NO_CONTEXT || g.surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Cannot create EGL context or surface\n");
        return false;
    }
    eglMakeCurrent(g.display, g.surface, g.surface, g.context);
    return true;
}

static void render_frame(void)
{
    inputs_t inputs = {};
    int i;

    inputs.window_size[0] = g.w / g.scale;
    inputs.window_size[1] = g.h / g.scale;
    inputs.scale = g.scale;
    for (i = 0; i < 4; i++) {
        vec2_copy(g.fingers[i].pos, inputs.touches[i].pos);
        inputs.touches[i].down[0] = g.fingers[i].down;
    }
    GL(glViewport(0, 0, g.w, g.h));
    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    goxel_iter(&inputs);
    goxel_render(&inputs);
}

static void frames(int n)
{
    while (n-- > 0) render_frame();
}

static void screenshot(const char *path)
{
    uint8_t *buf = malloc(g.w * g.h * 4);
    uint8_t *img = malloc(g.w * g.h * 3);
    int x, y;

    GL(glReadPixels(0, 0, g.w, g.h, GL_RGBA, GL_UNSIGNED_BYTE, buf));
    for (y = 0; y < g.h; y++)
    for (x = 0; x < g.w; x++)
        memcpy(&img[(y * g.w + x) * 3],
               &buf[((g.h - y - 1) * g.w + x) * 4], 3);
    img_write(img, g.w, g.h, 3, path);
    free(buf);
    free(img);
    printf("shot %s\n", path);
}

static void set_finger(int id, float x, float y, bool down)
{
    if (id < 0 || id >= 4) return;
    g.fingers[id].pos[0] = x;
    g.fingers[id].pos[1] = y;
    g.fingers[id].down = down;
}

static bool run_line(char *line)
{
    char cmd[32], arg[1024];
    int id, n, i;
    float x, y, x1, y1, scale;

    if (line[0] == '#' || sscanf(line, "%31s", cmd) != 1) return true;
    if (strcmp(cmd, "autosave_dir") == 0) {
        sscanf(line, "%*s %1023s", g.autosave_dir);
    } else if (strcmp(cmd, "autosave") == 0) {
        autosave(true);
    } else if (strcmp(cmd, "size") == 0) {
        if (sscanf(line, "%*s %d %d %f", &g.w, &g.h, &scale) == 3)
            g.scale = scale;
    } else if (strcmp(cmd, "frames") == 0) {
        if (sscanf(line, "%*s %d", &n) == 1) frames(n);
    } else if (strcmp(cmd, "down") == 0) {
        if (sscanf(line, "%*s %d %f %f", &id, &x, &y) == 3)
            set_finger(id, x, y, true);
        frames(1);
    } else if (strcmp(cmd, "move") == 0) {
        if (sscanf(line, "%*s %d %f %f", &id, &x, &y) == 3)
            set_finger(id, x, y, true);
        frames(1);
    } else if (strcmp(cmd, "up") == 0) {
        if (sscanf(line, "%*s %d", &id) == 1 && id >= 0 && id < 4)
            g.fingers[id].down = false;
        frames(1);
    } else if (strcmp(cmd, "tap") == 0) {
        if (sscanf(line, "%*s %f %f", &x, &y) != 2) return false;
        set_finger(0, x, y, true);
        frames(3);
        g.fingers[0].down = false;
        frames(3);
    } else if (strcmp(cmd, "drag") == 0) {
        if (sscanf(line, "%*s %f %f %f %f %d", &x, &y, &x1, &y1, &n) != 5)
            return false;
        set_finger(0, x, y, true);
        frames(1);
        for (i = 1; i <= n; i++) {
            set_finger(0, mix(x, x1, (float)i / n), mix(y, y1, (float)i / n),
                       true);
            frames(1);
        }
        g.fingers[0].down = false;
        frames(2);
    } else if (strcmp(cmd, "back") == 0) {
        gui_on_back();
        frames(3);
        if (goxel.quit) {
            printf("quit\n");
            goxel.quit = false;
        }
    } else if (strcmp(cmd, "shot") == 0) {
        if (sscanf(line, "%*s %1023s", arg) == 1) screenshot(arg);
    } else if (strcmp(cmd, "log") == 0) {
        printf("%s", line + 4);
    } else {
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    FILE *file;
    char line[2048], home[1024];
    int lineno = 0;
    bool initialized = false;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s script.txt\n", argv[0]);
        return 1;
    }
    file = fopen(argv[1], "r");
    if (!file) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    sys_callbacks.log = sim_log;
    // Fresh settings for each run.
    snprintf(home, sizeof(home), "/tmp/goxel-touchsim-%d", getpid());
    setenv("HOME", home, 1);

    while (fgets(line, sizeof(line), file)) {
        lineno++;
        // The size command must come before the first frame.
        if (!initialized && strncmp(line, "size", 4) != 0 &&
                strncmp(line, "autosave_dir", 12) != 0 &&
                line[0] != '#' && line[0] != '\n') {
            if (!egl_init()) return 1;
            goxel_init();
            if (g.autosave_dir[0]) {
                autosave_init(g.autosave_dir);
                autosave_check_restore();
            }
            frames(2);
            initialized = true;
        }
        if (!run_line(line))
            fprintf(stderr, "%s:%d: cannot parse: %s", argv[1], lineno, line);
    }
    fclose(file);
    return 0;
}
