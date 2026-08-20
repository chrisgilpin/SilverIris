#include <math.h>
#include <stdio.h>

#include "player/move.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    int l, t, w, h;
    float hfov43, hfov169;

    port_set_player_count(4);
    port_player_spawn();
    if (port_view_unsplit())
        return fail("spawn should leave split-screen");
    port_viewport(1, &l, &t, &w, &h);
    if (l != 0xA1 || w != 159)
        return fail("4P split P1 before unsplit");

    /* Remote seat 2: full-frame Hor+ on a 16:9 canvas. */
    port_set_view_seat(2);
    currentPlayerSetScreenSize(640.0f, 360.0f);
    currentPlayerSetScreenPosition(0.0f, 0.0f);
    currentPlayerSetPerspective(30.0f, 60.0f, 640.0f / 360.0f);
    if (!port_view_unsplit())
        return fail("unsplit on");
    if (port_view_seat() != 2)
        return fail("view seat");
    if (port_cur_player() != 2)
        return fail("cur follows view seat");
    port_viewport(2, &l, &t, &w, &h);
    if (l != 0 || t != 0 || w != 640 || h != 360)
        return fail("full-frame view-seat viewport");

    /* Other seats keep original split viewports (sim still has 4 players). */
    port_viewport(1, &l, &t, &w, &h);
    if (l != 0xA1 || t != 10 || w != 159 || h != 109)
        return fail("non-view seat stays split");
    if (port_player_count() != 4)
        return fail("sim still 4P");
    if (port_player_z_at(3) != 40.0f)
        return fail("P3 still spawned");

    currentPlayerSetPerspective(30.0f, 60.0f, 4.0f / 3.0f);
    hfov43 = port_view_hfov();
    currentPlayerSetPerspective(30.0f, 60.0f, 16.0f / 9.0f);
    hfov169 = port_view_hfov();
    if (!(hfov43 > 74.0f && hfov43 < 76.0f)) {
        fprintf(stderr, "4:3 hfov=%g\n", (double)hfov43);
        return fail("Hor+ 4:3");
    }
    if (!(hfov169 > 90.0f && hfov169 < 93.0f)) {
        fprintf(stderr, "16:9 hfov=%g\n", (double)hfov169);
        return fail("Hor+ 16:9");
    }
    if (!(hfov169 > hfov43))
        return fail("wider aspect must widen hfov");

    port_set_view_seat(-1);
    if (port_view_unsplit())
        return fail("unsplit off");
    port_viewport(1, &l, &t, &w, &h);
    if (l != 0xA1 || w != 159)
        return fail("back to local split");

    printf("view-seat ok full=%dx%d hfov43=%g hfov169=%g\n", 640, 360,
        (double)hfov43, (double)hfov169);
    return 0;
}
