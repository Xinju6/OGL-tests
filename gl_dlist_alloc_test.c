/*
 * Display-list allocation regression test.
 *
 * Covers the fix for the regression introduced by switching
 * _mesa_HashFindFreeKeyBlock to util_idalloc_alloc_range, which returns
 * 32-aligned block starts.  On a fresh context that allocator returns 32
 * instead of 1, breaking the glretrace wglUseFontBitmapsW reimplementation
 * that passes the raw (un-remapped) Windows listBase to glListBase.
 *
 * Tests:
 *   1. glGenLists returns 1 on a fresh context (not 32).
 *   2. Sequential glGenLists calls return contiguous IDs.
 *   3. Deleted names are NOT reused (reuse_names=false for DisplayList).
 *   4. Rendering: glCallLists with glListBase(base) executes the correct
 *      lists even when base equals the raw trace value (glretrace scenario).
 *
 * Build (Ubuntu):
 *   gcc -O2 -o gl_dlist_alloc_test tests/gl_dlist_alloc_test.c -lGL -lglut -lm
 *   sudo apt-get install freeglut3-dev libgl-dev   # if needed
 *
 * Run:
 *   ./gl_dlist_alloc_test
 *   Exit code 0 = all tests passed, 1 = one or more failures.
 */

#include <GL/gl.h>
#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---------- helpers ------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, bool ok)
{
    printf("  %s: %s\n", ok ? "PASS" : "FAIL", name);
    if (ok)
        g_pass++;
    else
        g_fail++;
}

/* Encode expected color for display list index c (0..255).
 * Using prime-multiply so adjacent chars differ visibly.
 * Returns packed 0xRRGGBB. */
static unsigned encode_color(unsigned c)
{
    return ((c * 251u) & 0xff) << 16 |
           ((c * 197u) & 0xff) <<  8 |
           ((c * 173u) & 0xff);
}

static void set_color(unsigned c)
{
    unsigned rgb = encode_color(c);
    glColor3ub((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

/* Draw a full-window solid quad with the color for display-list index c. */
static GLuint build_color_lists(GLuint base, int count)
{
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);

    for (int c = 0; c < count; c++) {
        glNewList(base + c, GL_COMPILE);
        set_color(c);
        glBegin(GL_QUADS);
        glVertex2i(0, 0);
        glVertex2i(w, 0);
        glVertex2i(w, h);
        glVertex2i(0, h);
        glEnd();
        glEndList();
    }
    return base;
}

/* Read back the center pixel, return packed 0xRRGGBB. */
static unsigned read_center_pixel(void)
{
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);
    unsigned char px[3];
    glReadPixels(w / 2, h / 2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
    return ((unsigned)px[0] << 16) | ((unsigned)px[1] << 8) | px[2];
}

/* Render character ch using glListBase(base)/glCallLists and return the
 * center pixel color. */
static unsigned render_char(GLuint base, unsigned char ch)
{
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glListBase(base);
    glCallLists(1, GL_UNSIGNED_BYTE, &ch);

    glFinish();
    return read_center_pixel();
}

/* ---------- test sections ------------------------------------------------- */

static void test_allocation(void)
{
    printf("\n--- Test 1: allocation starts at 1 ---\n");

    /*
     * On a fresh context the only reserved ID is 0.  glGenLists must return 1.
     * Before the fix util_idalloc_alloc_range returned 32 (first fully-zero
     * 32-bit element, skipping element 0 because bit 0 is set for ID 0).
     */
    GLuint base1 = glGenLists(256);
    check("glGenLists(256) == 1", base1 == 1);

    printf("\n--- Test 2: sequential allocation ---\n");

    GLuint base2 = glGenLists(256);
    check("second glGenLists(256) == 257", base2 == 257);

    printf("\n--- Test 3: no name reuse after glDeleteLists ---\n");

    glDeleteLists(base1, 256);  /* free names 1..256 */
    GLuint base3 = glGenLists(256);
    check("glGenLists after delete returns 513 (not 1)",
          base3 == 257 + 256);  /* 513 */

    /* Second delete + alloc */
    glDeleteLists(base2, 256);
    glDeleteLists(base3, 256);
    GLuint base4 = glGenLists(1);
    check("single glGenLists after all deletes still advances (769)",
          base4 == 513 + 256);  /* 769 */

    glDeleteLists(base4, 1);
}

static void test_rendering(void)
{
    printf("\n--- Test 4: rendering correctness (glListBase/glCallLists) ---\n");

    /*
     * Simulate the wglUseFontBitmapsW glretrace scenario:
     *   - Windows trace captured glGenLists → returned 1
     *   - Replay calls glGenLists → must also return 1 (with fix)
     *   - glListBase uses the raw trace value 1 (un-remapped)
     *   - glCallLists('A') must execute the list for 'A', not for '"'
     */
    GLuint replay_base = glGenLists(256);
    check("replay_base == 1 (prerequisite for rendering test)", replay_base == 1);

    build_color_lists(replay_base, 256);

    /* The trace-side listBase is 1 (Windows returned 1 from glGenLists). */
    GLuint trace_base = 1;

    /* Test several characters: for each char ch,
     *   glListBase(trace_base) + glCallLists(ch)
     *   executes list (trace_base + ch).
     *   With fix:    trace_base == replay_base == 1,
     *                so list (1+ch) == list for color(ch)  ✓
     *   Without fix: replay_base == 32, trace_base == 1,
     *                list (1+ch) holds color(ch - 31) ≠ color(ch)  ✗
     */
    const unsigned char test_chars[] = { 'A', 'G', 'Z', ' ', '0', 127 };
    char label[64];

    for (int i = 0; i < (int)(sizeof(test_chars) / sizeof(test_chars[0])); i++) {
        unsigned char ch = test_chars[i];
        unsigned got    = render_char(trace_base, ch);
        unsigned expect = encode_color(ch);
        snprintf(label, sizeof(label),
                 "char 0x%02x: pixel 0x%06x == expected 0x%06x", ch, got, expect);
        check(label, got == expect);
    }

    glDeleteLists(replay_base, 256);

    printf("\n--- Test 5: glCallLists after delete+realloc ---\n");

    /*
     * Verify that after deleting the font lists and allocating a new block,
     * the new block is at replay_base + 256 (no name reuse), so stale lists
     * from the first block are not accidentally called.
     */
    GLuint new_base = glGenLists(256);
    check("new_base == 257 after delete (no reuse)", new_base == 257);

    build_color_lists(new_base, 256);

    /* Using glListBase(new_base): char 'A' should still render correctly. */
    for (int i = 0; i < (int)(sizeof(test_chars) / sizeof(test_chars[0])); i++) {
        unsigned char ch = test_chars[i];
        unsigned got    = render_char(new_base, ch);
        unsigned expect = encode_color(ch);
        snprintf(label, sizeof(label),
                 "after realloc char 0x%02x: pixel 0x%06x == expected 0x%06x",
                 ch, got, expect);
        check(label, got == expect);
    }

    glDeleteLists(new_base, 256);
}

/* ---------- GLUT callbacks ------------------------------------------------ */

static bool g_done = false;

static void display(void)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    test_allocation();
    test_rendering();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    g_done = true;

    glutSwapBuffers();
    exit(g_fail > 0 ? 1 : 0);
}

static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q' || key == 'Q')
        exit(g_fail > 0 ? 1 : 0);
}

/* ---------- main ---------------------------------------------------------- */

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(64, 64);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("GL display-list alloc test");

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);

    glutMainLoop();
    return 1; /* unreachable */
}
