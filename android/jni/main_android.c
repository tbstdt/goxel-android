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
 * Android entry point: replaces the GLFW based main.c.
 *
 * Uses android_native_app_glue (NativeActivity) + EGL.  File dialogs are
 * forwarded to the Java side (GoxelActivity) that uses the Storage Access
 * Framework.  Since the goxel API expects the dialogs to be synchronous, we
 * keep pumping the android event loop (without rendering) until the Java
 * side posts the result back.
 */

#include "goxel.h"

#include <EGL/egl.h>
#include <errno.h>
#include <unistd.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>

#define TAG "goxel"

static struct {
    struct android_app *app;
    EGLDisplay  display;
    EGLSurface  surface;
    EGLContext  context;
    EGLConfig   config;
    bool        goxel_initialized;
    bool        resumed;
    float       scale;

    // Persistent pointer state, copied into the goxel inputs each frame.
    struct {
        int32_t id;     // Android pointer id, -1 if the slot is free.
        float   pos[2];
        bool    down[3];
    } slots[4];
    float       hover_pos[2];
    float       wheel;
    bool        touches_canceled;
    bool        keys[512];
    uint32_t    chars[16];
    int         nb_chars;

    // File dialog result, set from the java UI thread.
    pthread_mutex_t dialog_mutex;
    atomic_bool dialog_done;
    char        dialog_path[1024];
    bool        dialog_ok;

    // File sent by another app ("open with"), set from the java UI thread.
    char        open_path[1024];
    bool        open_import;

} g = {
    .display = EGL_NO_DISPLAY,
    .surface = EGL_NO_SURFACE,
    .context = EGL_NO_CONTEXT,
    .dialog_mutex = PTHREAD_MUTEX_INITIALIZER,
};

static void android_log(void *user, const char *msg)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, msg);
}

/* ---------------------------------------------------------------------- */
/* JNI helpers                                                            */

static JNIEnv *get_env(void)
{
    JNIEnv *env;
    JavaVM *vm = g.app->activity->vm;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    return env;
}

static jmethodID get_method(JNIEnv *env, const char *name, const char *sig)
{
    jclass cls = (*env)->GetObjectClass(env, g.app->activity->clazz);
    jmethodID ret = (*env)->GetMethodID(env, cls, name, sig);
    (*env)->DeleteLocalRef(env, cls);
    return ret;
}

JNIEXPORT void JNICALL
Java_org_goxel_android_GoxelActivity_nativeDialogResult(
        JNIEnv *env, jclass cls, jstring path)
{
    const char *str;
    pthread_mutex_lock(&g.dialog_mutex);
    g.dialog_ok = path != NULL;
    g.dialog_path[0] = '\0';
    if (path) {
        str = (*env)->GetStringUTFChars(env, path, NULL);
        snprintf(g.dialog_path, sizeof(g.dialog_path), "%s", str);
        (*env)->ReleaseStringUTFChars(env, path, str);
    }
    atomic_store(&g.dialog_done, true);
    pthread_mutex_unlock(&g.dialog_mutex);
    if (g.app && g.app->looper) ALooper_wake(g.app->looper);
}

/*
 * Called from the java UI thread when another app asks us to open a file
 * (already copied locally).  If import is set, the file is opened as a new
 * image without a save path (we can't write the original document).
 */
JNIEXPORT void JNICALL
Java_org_goxel_android_GoxelActivity_nativeOpenFile(
        JNIEnv *env, jclass cls, jstring path, jboolean import)
{
    const char *str = (*env)->GetStringUTFChars(env, path, NULL);
    pthread_mutex_lock(&g.dialog_mutex);
    snprintf(g.open_path, sizeof(g.open_path), "%s", str);
    g.open_import = import;
    pthread_mutex_unlock(&g.dialog_mutex);
    (*env)->ReleaseStringUTFChars(env, path, str);
    if (g.app && g.app->looper) ALooper_wake(g.app->looper);
}

static void open_pending_file(void)
{
    char path[1024];
    bool import;

    pthread_mutex_lock(&g.dialog_mutex);
    snprintf(path, sizeof(path), "%s", g.open_path);
    import = g.open_import;
    g.open_path[0] = '\0';
    pthread_mutex_unlock(&g.dialog_mutex);
    if (!path[0]) return;
    goxel_open_file_ex(path, import);
    if (!import && str_endswith(path, ".gox")) goxel_add_recent_file(path);
}

static void show_keyboard(void *user, bool show)
{
    static bool shown = false;
    JNIEnv *env;
    if (show == shown) return;
    shown = show;
    env = get_env();
    (*env)->CallVoidMethod(env, g.app->activity->clazz,
                           get_method(env, "showKeyboard", "(Z)V"),
                           (jboolean)show);
}

/* ---------------------------------------------------------------------- */
/* EGL                                                                    */

static bool egl_init_display(void)
{
    EGLint nb;
    const EGLint attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

    if (g.display != EGL_NO_DISPLAY) return true;
    g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g.display, NULL, NULL);
    if (!eglChooseConfig(g.display, attrs, &g.config, 1, &nb) || nb < 1) {
        LOG_E("No suitable EGL config");
        return false;
    }
    g.context = eglCreateContext(g.display, g.config, EGL_NO_CONTEXT,
                                 ctx_attrs);
    if (g.context == EGL_NO_CONTEXT) {
        LOG_E("Cannot create GLES3 context");
        return false;
    }
    return true;
}

static bool egl_create_surface(ANativeWindow *window)
{
    if (!egl_init_display()) return false;
    g.surface = eglCreateWindowSurface(g.display, g.config, window, NULL);
    if (g.surface == EGL_NO_SURFACE) {
        LOG_E("Cannot create EGL surface");
        return false;
    }
    eglMakeCurrent(g.display, g.surface, g.surface, g.context);
    eglSwapInterval(g.display, 1);
    return true;
}

// We keep the context alive when the window goes away so that we don't
// have to recreate all the goxel GL objects.
static void egl_destroy_surface(void)
{
    if (g.surface == EGL_NO_SURFACE) return;
    eglMakeCurrent(g.display, EGL_NO_SURFACE, EGL_NO_SURFACE, g.context);
    eglDestroySurface(g.display, g.surface);
    g.surface = EGL_NO_SURFACE;
}

/* ---------------------------------------------------------------------- */
/* Inputs                                                                 */

static int key_from_android(int32_t code)
{
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z)
        return 'A' + code - AKEYCODE_A;
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9)
        return '0' + code - AKEYCODE_0;
    switch (code) {
    case AKEYCODE_SPACE:        return ' ';
    case AKEYCODE_ESCAPE:       return KEY_ESCAPE;
    case AKEYCODE_ENTER:        return KEY_ENTER;
    case AKEYCODE_TAB:          return KEY_TAB;
    case AKEYCODE_DEL:          return KEY_BACKSPACE;
    case AKEYCODE_FORWARD_DEL:  return KEY_DELETE;
    case AKEYCODE_DPAD_RIGHT:   return KEY_RIGHT;
    case AKEYCODE_DPAD_LEFT:    return KEY_LEFT;
    case AKEYCODE_DPAD_DOWN:    return KEY_DOWN;
    case AKEYCODE_DPAD_UP:      return KEY_UP;
    case AKEYCODE_PAGE_UP:      return KEY_PAGE_UP;
    case AKEYCODE_PAGE_DOWN:    return KEY_PAGE_DOWN;
    case AKEYCODE_MOVE_HOME:    return KEY_HOME;
    case AKEYCODE_MOVE_END:     return KEY_END;
    case AKEYCODE_SHIFT_LEFT:   return KEY_LEFT_SHIFT;
    case AKEYCODE_SHIFT_RIGHT:  return KEY_RIGHT_SHIFT;
    case AKEYCODE_CTRL_LEFT:    return KEY_LEFT_CONTROL;
    case AKEYCODE_CTRL_RIGHT:   return KEY_RIGHT_CONTROL;
    case AKEYCODE_ALT_LEFT:     return KEY_LEFT_ALT;
    default:                    return -1;
    }
}

static int find_slot(int32_t id, bool alloc)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(g.slots); i++)
        if (g.slots[i].down[0] && g.slots[i].id == id) return i;
    if (!alloc) return -1;
    for (i = 0; i < ARRAY_SIZE(g.slots); i++)
        if (!g.slots[i].down[0]) return i;
    return -1;
}

static void release_all_slots(void)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(g.slots); i++) {
        memset(g.slots[i].down, 0, sizeof(g.slots[i].down));
        g.slots[i].id = -1;
    }
    g.touches_canceled = true;
}

static int32_t on_motion_event(const AInputEvent *event)
{
    int32_t action = AMotionEvent_getAction(event);
    int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                    AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    int32_t tool = AMotionEvent_getToolType(event, 0);
    int32_t buttons;
    size_t i, nb;
    int s;
    float x, y;

    // Mouse (DeX, chromebook, bluetooth mouse): use the buttons state.
    if (tool == AMOTION_EVENT_TOOL_TYPE_MOUSE) {
        x = AMotionEvent_getX(event, 0) / g.scale;
        y = AMotionEvent_getY(event, 0) / g.scale;
        g.hover_pos[0] = x;
        g.hover_pos[1] = y;
        if (masked == AMOTION_EVENT_ACTION_SCROLL) {
            g.wheel += AMotionEvent_getAxisValue(
                    event, AMOTION_EVENT_AXIS_VSCROLL, 0);
            return 1;
        }
        buttons = AMotionEvent_getButtonState(event);
        g.slots[0].id = 0;
        g.slots[0].pos[0] = x;
        g.slots[0].pos[1] = y;
        g.slots[0].down[0] = buttons & AMOTION_EVENT_BUTTON_PRIMARY;
        g.slots[0].down[1] = buttons & AMOTION_EVENT_BUTTON_TERTIARY;
        g.slots[0].down[2] = buttons & AMOTION_EVENT_BUTTON_SECONDARY;
        return 1;
    }

    if (masked == AMOTION_EVENT_ACTION_HOVER_MOVE) {
        g.hover_pos[0] = AMotionEvent_getX(event, 0) / g.scale;
        g.hover_pos[1] = AMotionEvent_getY(event, 0) / g.scale;
        return 1;
    }

    switch (masked) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        s = find_slot(AMotionEvent_getPointerId(event, index), true);
        if (s < 0) break;
        g.slots[s].id = AMotionEvent_getPointerId(event, index);
        g.slots[s].pos[0] = AMotionEvent_getX(event, index) / g.scale;
        g.slots[s].pos[1] = AMotionEvent_getY(event, index) / g.scale;
        g.slots[s].down[0] = true;
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
        s = find_slot(AMotionEvent_getPointerId(event, index), false);
        if (s < 0) break;
        g.slots[s].pos[0] = AMotionEvent_getX(event, index) / g.scale;
        g.slots[s].pos[1] = AMotionEvent_getY(event, index) / g.scale;
        g.slots[s].down[0] = false;
        g.slots[s].id = -1;
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        release_all_slots();
        break;
    }

    // Update all the positions.
    nb = AMotionEvent_getPointerCount(event);
    for (i = 0; i < nb; i++) {
        s = find_slot(AMotionEvent_getPointerId(event, i), false);
        if (s < 0) continue;
        g.slots[s].pos[0] = AMotionEvent_getX(event, i) / g.scale;
        g.slots[s].pos[1] = AMotionEvent_getY(event, i) / g.scale;
    }
    return 1;
}

// The NDK has no API to get the unicode value of a key, so ask java.
static int get_unicode_char(int32_t code, int32_t meta)
{
    JNIEnv *env = get_env();
    return (*env)->CallIntMethod(env, g.app->activity->clazz,
            get_method(env, "getUnicodeChar", "(II)I"), code, meta);
}

static int32_t on_key_event(const AInputEvent *event)
{
    int32_t action = AKeyEvent_getAction(event);
    int32_t code = AKeyEvent_getKeyCode(event);
    int key = key_from_android(code);
    int unicode;

    if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN)
        return 0;
    if (code == AKEYCODE_BACK) {
        if (action == AKEY_EVENT_ACTION_UP) gui_on_back();
        return 1;
    }
    if (action == AKEY_EVENT_ACTION_DOWN) {
        unicode = get_unicode_char(code, AKeyEvent_getMetaState(event));
        if (unicode >= 32 && g.nb_chars < ARRAY_SIZE(g.chars))
            g.chars[g.nb_chars++] = unicode;
    }
    if (key < 0) return 1;
    if (action == AKEY_EVENT_ACTION_DOWN) g.keys[key] = true;
    if (action == AKEY_EVENT_ACTION_UP) g.keys[key] = false;
    return 1;
}

static int32_t on_input_event(struct android_app *app, AInputEvent *event)
{
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_MOTION: return on_motion_event(event);
    case AINPUT_EVENT_TYPE_KEY: return on_key_event(event);
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* App lifecycle                                                          */

static void update_scale(void)
{
    int32_t density = AConfiguration_getDensity(g.app->config);
    if (density <= 0 || density == ACONFIGURATION_DENSITY_ANY)
        density = ACONFIGURATION_DENSITY_MEDIUM;
    g.scale = (float)density / ACONFIGURATION_DENSITY_MEDIUM;
}

static void on_app_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) egl_create_surface(app->window);
        break;
    case APP_CMD_TERM_WINDOW:
        egl_destroy_surface();
        release_all_slots();
        break;
    case APP_CMD_RESUME:
        g.resumed = true;
        break;
    case APP_CMD_PAUSE:
        g.resumed = false;
        release_all_slots();
        // Android can kill us any time once in the background.
        if (g.goxel_initialized) autosave(true);
        break;
    case APP_CMD_SAVE_STATE:
        if (g.goxel_initialized) autosave(true);
        break;
    case APP_CMD_CONFIG_CHANGED:
        update_scale();
        break;
    }
}

// Process all the pending events.  If block is set, wait for at least one.
static void pump_events(bool block)
{
    int events;
    struct android_poll_source *source;
    while (ALooper_pollAll(block ? -1 : 0, NULL, &events,
                           (void**)&source) >= 0) {
        if (source) source->process(g.app, source);
        if (g.app->destroyRequested) return;
        block = false;
    }
}

/* ---------------------------------------------------------------------- */
/* File dialogs                                                           */

static bool open_dialog(
        void *user,
        char *buf,
        size_t buf_size,
        int flags,
        const char *title,
        const char *default_path_and_file,
        int nb_filters,
        const char *const *filters,
        const char *filters_desc)
{
    JNIEnv *env = get_env();
    char default_name[256] = "";
    char exts[256] = "";
    int i;
    size_t len = 0;
    jstring jname, jexts;

    if (flags & 2) return false; // Folder dialog not supported yet.
    if (default_path_and_file)
        path_basename(default_path_and_file, default_name,
                      sizeof(default_name));
    for (i = 0; i < nb_filters; i++) {
        // Filters are like "*.vox".
        len += snprintf(exts + len, sizeof(exts) - len, "%s%s",
                        i ? "," : "", filters[i] + 2);
    }

    atomic_store(&g.dialog_done, false);
    jname = (*env)->NewStringUTF(env, default_name);
    jexts = (*env)->NewStringUTF(env, exts);
    (*env)->CallVoidMethod(env, g.app->activity->clazz,
            get_method(env, "openDialog",
                       "(ZLjava/lang/String;Ljava/lang/String;)V"),
            (jboolean)(flags & 1), jname, jexts);
    (*env)->DeleteLocalRef(env, jname);
    (*env)->DeleteLocalRef(env, jexts);

    // Keep the android event loop running while the picker is visible.
    while (!atomic_load(&g.dialog_done) && !g.app->destroyRequested)
        pump_events(true);
    release_all_slots();

    pthread_mutex_lock(&g.dialog_mutex);
    if (g.dialog_ok) {
        // A local copy of the document, see GoxelActivity.
        snprintf(buf, buf_size, "%s", g.dialog_path);
    }
    pthread_mutex_unlock(&g.dialog_mutex);
    return g.dialog_ok;
}

// Copy the local copies goxel wrote back to their documents.
static void sync_documents(void)
{
    static jmethodID method = NULL;
    JNIEnv *env = get_env();
    if (!method) method = get_method(env, "syncDocuments", "()V");
    (*env)->CallVoidMethod(env, g.app->activity->clazz, method);
}

static void on_open_file(void *user, const char *path)
{
    JNIEnv *env = get_env();
    jstring jpath = (*env)->NewStringUTF(env, path);
    (*env)->CallVoidMethod(env, g.app->activity->clazz,
            get_method(env, "refreshDocument", "(Ljava/lang/String;)V"),
            jpath);
    (*env)->DeleteLocalRef(env, jpath);
}

/* ---------------------------------------------------------------------- */
/* Main loop                                                              */

static void render_frame(inputs_t *inputs)
{
    EGLint w, h;
    int i;

    eglQuerySurface(g.display, g.surface, EGL_WIDTH, &w);
    eglQuerySurface(g.display, g.surface, EGL_HEIGHT, &h);

    memset(inputs, 0, sizeof(*inputs));
    inputs->window_size[0] = w / g.scale;
    inputs->window_size[1] = h / g.scale;
    inputs->scale = g.scale;
    memcpy(inputs->keys, g.keys, sizeof(g.keys));
    memcpy(inputs->chars, g.chars, g.nb_chars * sizeof(g.chars[0]));
    g.nb_chars = 0;
    inputs->mouse_wheel = g.wheel;
    g.wheel = 0;
    inputs->touches_canceled = g.touches_canceled;
    g.touches_canceled = false;

    for (i = 0; i < ARRAY_SIZE(g.slots); i++) {
        vec2_copy(g.slots[i].pos, inputs->touches[i].pos);
        memcpy(inputs->touches[i].down, g.slots[i].down,
               sizeof(inputs->touches[i].down));
    }
    // Without any finger down, the first touch is the hover position.
    if (g.slots[0].id == -1) vec2_copy(g.hover_pos, inputs->touches[0].pos);
    else vec2_copy(g.slots[0].pos, g.hover_pos);

    GL(glViewport(0, 0, w, h));
    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    goxel_iter(inputs);
    goxel_render(inputs);
    sync_documents();
    autosave(false);
    eglSwapBuffers(g.display, g.surface);
}

void android_main(struct android_app *app)
{
    inputs_t inputs = {};
    char home[1024];

    g.app = app;
    app->onAppCmd = on_app_cmd;
    app->onInputEvent = on_input_event;
    release_all_slots();
    update_scale();

    sys_callbacks.log = android_log;
    sys_callbacks.open_dialog = open_dialog;
    sys_callbacks.show_keyboard = show_keyboard;
    sys_callbacks.on_open_file = on_open_file;
    // goxel stores its settings in $HOME/.config/goxel.
    snprintf(home, sizeof(home), "%s", app->activity->internalDataPath);
    setenv("HOME", home, 1);

    while (!app->destroyRequested) {
        pump_events(!g.resumed || g.surface == EGL_NO_SURFACE);
        if (app->destroyRequested) break;
        if (!g.resumed || g.surface == EGL_NO_SURFACE) continue;
        if (!g.goxel_initialized) {
            goxel_init();
            g.goxel_initialized = true;
            autosave_init(g.app->activity->internalDataPath);
            autosave_check_restore();
        }
        open_pending_file();
        render_frame(&inputs);
        if (goxel.quit) {
            // The user chose to quit: nothing to restore.
            autosave_delete();
            ANativeActivity_finish(app->activity);
            goxel.quit = false;
        }
    }

    // Note: we don't release goxel, android will kill the process.
    egl_destroy_surface();
}
