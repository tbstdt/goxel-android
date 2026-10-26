/* Goxel 3D voxels editor
 *
 * copyright (c) 2017 Guillaume Chereau <guillaume@noctua-software.com>
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

#include "goxel.h"

// Still experimental gestures manager.

// XXX: this value should be set depending on the screen resolution.
static float g_start_dist = 8;

static int g_emulate_three_buttons_mouse = 0;

void gesture_set_emulate_three_buttons_mouse(int key)
{
    g_emulate_three_buttons_mouse = key;
}

static bool test_button(const inputs_t *inputs, const touch_t *touch, int mask)
{
    bool lmb = touch->down[0];
    bool mmb = touch->down[1];
    bool rmb = touch->down[2];
    bool ctrl;
    bool shift;

    ctrl = inputs->keys[KEY_LEFT_CONTROL] || inputs->keys[KEY_RIGHT_CONTROL];
    shift = inputs->keys[KEY_LEFT_SHIFT] || inputs->keys[KEY_RIGHT_SHIFT];

    if (g_emulate_three_buttons_mouse &&
        inputs->keys[g_emulate_three_buttons_mouse]) {
        mmb = lmb;
        lmb = false;
    }

    if (mask & GESTURE_SHIFT && !shift) return false;
    if (mask & GESTURE_CTRL && !ctrl) return false;
    if ((mask & GESTURE_LMB) && !lmb) return false;
    if ((mask & GESTURE_MMB) && !mmb) return false;
    if ((mask & GESTURE_RMB) && !rmb) return false;
    return true;
}

static bool rect_contains(const float rect[4], const float pos[2])
{
    return pos[0] >= rect[0] && pos[0] < rect[0] + rect[2] &&
           pos[1] >= rect[1] && pos[1] < rect[1] + rect[3];
}

static float get_angle(const float a0[2], const float a1[2],
                       const float b0[2], const float b1[2])
{
    float u[2], v[2], dot, det;
    vec2_sub(a1, a0, u);
    vec2_normalize(u, u);
    vec2_sub(b1, b0, v);
    vec2_normalize(v, v);
    dot = vec2_dot(u, v);
    det = vec2_cross(u, v);
    return atan2(det, dot);
}

// Max duration and movement for a multi fingers tap.
#define TAP_MAX_TIME 0.3
#define TAP_MAX_DIST 16

// Centroid of the fingers down, and mean distance to it.  Also set the
// indices of the first two fingers in idx.  Return the number of fingers.
static int get_fingers(const inputs_t *inputs, float center[2],
                       float *spread, int idx[2])
{
    const touch_t *ts = inputs->touches;
    int i, n = 0;

    vec2_set(center, 0, 0);
    *spread = 0;
    for (i = 0; i < ARRAY_SIZE(inputs->touches); i++) {
        if (!ts[i].down[0]) continue;
        if (n < 2) idx[n] = i;
        vec2_add(center, ts[i].pos, center);
        n++;
    }
    if (n == 0) return 0;
    vec2_mul(center, 1.0f / n, center);
    for (i = 0; i < ARRAY_SIZE(inputs->touches); i++) {
        if (ts[i].down[0]) *spread += vec2_dist(ts[i].pos, center) / n;
    }
    return n;
}

// Start (or restart after the number of fingers changed) the gesture.
static void multi_touch_start(gesture_t *gest, const inputs_t *inputs,
                              int n, const float center[2], float spread,
                              const int idx[2])
{
    gest->state = GESTURE_BEGIN;
    gest->nb_touches = n;
    gest->max_touches = max(gest->max_touches, n);
    gest->pinch = 1;
    gest->rotation = 0;
    gest->start_spread = max(spread, 1.0f);
    vec2_copy(inputs->touches[idx[0]].pos, gest->start_pos[0]);
    vec2_copy(inputs->touches[idx[1]].pos, gest->start_pos[1]);
    vec2_copy(center, gest->pos);
    vec2_copy(center, gest->last_pos);
}

static void update_multi_touch(gesture_t *gest, const inputs_t *inputs)
{
    const touch_t *ts = inputs->touches;
    float center[2], spread;
    int i, n, idx[2];

    n = get_fingers(inputs, center, &spread, idx);

    // Track each finger movement to detect taps.
    if (gest->state != GESTURE_POSSIBLE) {
        for (i = 0; i < ARRAY_SIZE(inputs->touches); i++) {
            if (!ts[i].down[0]) continue;
            if (!(gest->seen_mask & (1 << i))) {
                gest->seen_mask |= 1 << i;
                vec2_copy(ts[i].pos, gest->touch_start[i]);
            } else if (vec2_dist(ts[i].pos, gest->touch_start[i]) >
                       TAP_MAX_DIST) {
                gest->moved = true;
            }
        }
    }

    switch (gest->state) {
    case GESTURE_POSSIBLE:
        if (n == 0) gest->wait_up = false;
        if (n < 2 || gest->wait_up) break;
        gest->max_touches = 0;
        gest->moved = false;
        gest->tap = 0;
        gest->start_time = sys_get_time();
        gest->seen_mask = 0;
        for (i = 0; i < ARRAY_SIZE(inputs->touches); i++) {
            if (!ts[i].down[0]) continue;
            gest->seen_mask |= 1 << i;
            vec2_copy(ts[i].pos, gest->touch_start[i]);
        }
        multi_touch_start(gest, inputs, n, center, spread, idx);
        break;
    case GESTURE_BEGIN:
    case GESTURE_UPDATE:
        if (n < gest->nb_touches) {
            // Lifting a finger ends the gesture, we don't want to switch
            // from a three fingers pan to a two fingers rotation.
            gest->state = GESTURE_END;
            gest->wait_up = n > 0;
            // More than two fingers is likely a system gesture, and the
            // touches might have been canceled: no tap.
            if (!gest->moved && gest->max_touches == 2 &&
                    sys_get_time() - gest->start_time < TAP_MAX_TIME)
                gest->tap = gest->max_touches;
            break;
        }
        if (n > gest->nb_touches) {
            multi_touch_start(gest, inputs, n, center, spread, idx);
            break;
        }
        gest->state = GESTURE_UPDATE;
        vec2_copy(center, gest->pos);
        gest->pinch = spread / gest->start_spread;
        gest->rotation = get_angle(gest->start_pos[0], gest->start_pos[1],
                                   ts[idx[0]].pos, ts[idx[1]].pos);
        break;
    }
}

static int update(gesture_t *gest, const inputs_t *inputs, int mask)
{
    const touch_t *ts = inputs->touches;
    int nb_ts = 0;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(inputs->touches); i++) {
        for (j = 0; j < ARRAY_SIZE(inputs->touches[i].down); j++) {
            if (ts[i].down[j]) {
                nb_ts++;
                break;
            }
        }
    }

    if (gest->type == GESTURE_DRAG) {
        switch (gest->state) {
        case GESTURE_POSSIBLE:
            if (nb_ts == 1 && test_button(inputs, &ts[0], gest->button)) {
                vec2_copy(ts[0].pos, gest->start_pos[0]);
                vec2_copy(gest->start_pos[0], gest->pos);
                vec2_copy(gest->start_pos[0], gest->last_pos);
                if (!rect_contains(gest->viewport, gest->pos)) {
                    gest->state = GESTURE_FAILED;
                    break;
                }
                gest->state = (mask & (GESTURE_CLICK | GESTURE_PINCH)) ?
                            GESTURE_RECOGNISED : GESTURE_BEGIN;
            }
            break;
        case GESTURE_RECOGNISED:
            if (vec2_dist(gest->start_pos[0], ts[0].pos) >= g_start_dist)
                gest->state = GESTURE_BEGIN;
            if (nb_ts == 0) {
                gest->state = (!(mask & GESTURE_CLICK)) ?
                    GESTURE_BEGIN : GESTURE_FAILED;
            }
            if (nb_ts > 1) gest->state = GESTURE_FAILED;
            break;
        case GESTURE_BEGIN:
        case GESTURE_UPDATE:
            if (!test_button(inputs, &ts[0], gest->button)) {
                gest->state = GESTURE_END;
            } else {
                vec2_copy(ts[0].pos, gest->pos);
                gest->state = GESTURE_UPDATE;
            }
            break;
        }
    }

    if (gest->type == GESTURE_CLICK) {
        vec2_copy(ts[0].pos, gest->pos);
        switch (gest->state) {
        case GESTURE_POSSIBLE:
            if (test_button(inputs, &ts[0], gest->button)) {
                vec2_copy(ts[0].pos, gest->start_pos[0]);
                gest->state = GESTURE_RECOGNISED;
            }
            break;
        case GESTURE_RECOGNISED:
            if (!test_button(inputs, &ts[0], gest->button))
                gest->state = GESTURE_TRIGGERED;
            break;
        }
    }

    if (gest->type == GESTURE_PINCH) {
        update_multi_touch(gest, inputs);
    }

    if (gest->type == GESTURE_HOVER) {
        switch (gest->state) {
        case GESTURE_POSSIBLE:
            if (nb_ts == 0 && rect_contains(gest->viewport, ts[0].pos)) {
                vec2_copy(ts[0].pos, gest->pos);
                if (rect_contains(gest->viewport, gest->pos)) {
                    gest->state = GESTURE_BEGIN;
                }
            }
            break;
        case GESTURE_BEGIN:
        case GESTURE_UPDATE:
            vec2_copy(ts[0].pos, gest->pos);
            gest->state = GESTURE_UPDATE;
            if (!rect_contains(gest->viewport, gest->pos))
                gest->state = GESTURE_END;
            if (nb_ts != 0) {
                gest->state = GESTURE_END;
            }
            break;
        }
    }

    return 0;
}


int gesture_update(int nb, gesture_t *gestures[],
                   const inputs_t *inputs, const float viewport[4],
                   void *user)
{
    int i, j, mask = 0;
    bool allup = true; // Set if all the mouse buttons are up.
    gesture_t *gest, *triggered = NULL;

    for (i = 0; allup && i < ARRAY_SIZE(inputs->touches); i++) {
        for (j = 0; allup && j < ARRAY_SIZE(inputs->touches[i].down); j++) {
            if (inputs->touches[i].down[j]) {
                allup = false;
                break;
            }
        }
    }

    if (inputs->touches_canceled) {
        for (i = 0; i < nb; i++) {
            gest = gestures[i];
            if (gest->type == GESTURE_HOVER) continue;
            if (gest->state == GESTURE_BEGIN ||
                    gest->state == GESTURE_UPDATE) {
                gest->state = GESTURE_END;
                gest->tap = 0;
                gest->callback(gest, user);
            }
            gest->state = GESTURE_FAILED;
        }
        return 0;
    }

    for (i = 0; i < nb; i++) {
        gest = gestures[i];
        if (gest->state == GESTURE_POSSIBLE) mask |= gest->type;
    }

    for (i = 0; i < nb; i++) {
        gest = gestures[i];
        vec4_copy(viewport, gest->viewport);
        if ((gest->state == GESTURE_FAILED) && allup) {
            gest->state = GESTURE_POSSIBLE;
        }
        if (gest->state == GESTURE_END || gest->state == GESTURE_TRIGGERED)
            gest->state = GESTURE_POSSIBLE;

        update(gest, inputs, mask);
        if (    gest->state == GESTURE_BEGIN ||
                gest->state == GESTURE_UPDATE ||
                gest->state == GESTURE_END ||
                gest->state == GESTURE_TRIGGERED)
        {
            if (gest->callback(gest, user) != 0) {
                gest->state = GESTURE_FAILED;
                continue;
            }
            vec2_copy(gest->pos, gest->last_pos);

            if (    gest->state == GESTURE_BEGIN ||
                    gest->state == GESTURE_TRIGGERED) {
                triggered = gest;
                break;
            }
        }
    }

    // If one gesture started, fail all the other gestures.
    if (    triggered && triggered->type != GESTURE_HOVER &&
            triggered->state == GESTURE_BEGIN) {
        for (i = 0; i < nb; i++) {
            gest = gestures[i];
            if (gest != triggered)
                gest->state = GESTURE_FAILED;
        }
    }
    return triggered ? 1 : 0;
}
