/*
 * glCallLists bitmap font test.
 *
 * Draws a simple color scene and overlays two text lines rendered
 * via glCallLists/glBitmap display lists:
 *   Line 1: current date/time
 *   Line 2: elapsed time from program start to first rendered frame
 *
 * Build (Ubuntu):
 *   gcc -O2 -o glcalllists_date_test glcalllists_date_test.c -lGL -lglut -lm
 *   sudo apt-get install freeglut3-dev   # if needed
 *
 * Run:
 *   ./glcalllists_date_test
 *   Press Q or ESC to quit; results are also printed to stdout on exit.
 */

#include <GL/gl.h>
#include <GL/glut.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

/* ---------- timing --------------------------------------------------- */

static struct timespec g_start;

static double elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec  - g_start.tv_sec ) * 1000.0
         + (now.tv_nsec - g_start.tv_nsec) / 1e6;
}

/* ---------- font display lists --------------------------------------- */

/*
 * One display list per ASCII code 0-127.
 * Each list compiles a glutBitmapCharacter call, which internally
 * issues glBitmap — exactly the path exercised by the name-reuse bug.
 */
static GLuint g_font_base;   /* glGenLists return value */
static double g_font_build_ms;

static void build_font_lists(void)
{
    double t0 = elapsed_ms();

    g_font_base = glGenLists(128);
    for (int c = 0; c < 128; c++) {
        glNewList(g_font_base + c, GL_COMPILE);
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);
        glEndList();
    }

    g_font_build_ms = elapsed_ms() - t0;
    printf("glGenLists(128) returned %u  (build: %.3f ms)\n",
           g_font_base, g_font_build_ms);
}

/* Render a NUL-terminated ASCII string at window pixel coordinates (x,y).
 * y=0 is the bottom of the window (OpenGL convention). */
static void draw_string(float x, float y, const char *s)
{
    glRasterPos2f(x, y);
    glListBase(g_font_base);          /* base + char_code executes the list  */
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

/* ---------- scene geometry ------------------------------------------- */

static void draw_scene(int w, int h)
{
    /* Gradient background quad */
    glBegin(GL_QUADS);
    glColor3f(0.10f, 0.00f, 0.25f); glVertex2f(0,   0);
    glColor3f(0.00f, 0.20f, 0.55f); glVertex2f(w,   0);
    glColor3f(0.00f, 0.55f, 0.35f); glVertex2f(w,   (float)(h * 0.72));
    glColor3f(0.25f, 0.10f, 0.45f); glVertex2f(0,   (float)(h * 0.72));
    glEnd();

    /* RGB triangle */
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(w * 0.15f, h * 0.05f);
    glColor3f(0, 1, 0); glVertex2f(w * 0.85f, h * 0.05f);
    glColor3f(0, 0, 1); glVertex2f(w * 0.50f, h * 0.65f);
    glEnd();

    /* Yellow-cyan circle */
    {
        float cx = w * 0.50f, cy = h * 0.35f, r = h * 0.18f;
        glBegin(GL_TRIANGLE_FAN);
        glColor3f(1.0f, 1.0f, 0.2f);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 48; i++) {
            float a = i * (float)(2.0 * M_PI / 48);
            float cr = 0.2f + 0.8f * (float)i / 48;
            glColor3f(cr, 1.0f - cr * 0.5f, 0.8f);
            glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
        }
        glEnd();
    }

    /* Checkerboard strip along the bottom half */
    {
        int cols = 16, rows = 3;
        float cw = (float)w / cols, ch = (float)(h * 0.30) / rows;
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                float bri = ((row + col) & 1) ? 0.7f : 0.15f;
                glColor3f(bri, bri * 0.8f, bri * 1.2f > 1 ? 1 : bri * 1.2f);
                float x0 = col * cw, y0 = row * ch;
                glBegin(GL_QUADS);
                glVertex2f(x0,      y0);
                glVertex2f(x0 + cw, y0);
                glVertex2f(x0 + cw, y0 + ch);
                glVertex2f(x0,      y0 + ch);
                glEnd();
            }
        }
    }
}

/* ---------- text overlay --------------------------------------------- */

static char g_line1[128];   /* date/time */
static char g_line2[128];   /* elapsed time */

static void draw_text_overlay(int w, int h)
{
    float bar_y = (float)(h * 0.72);

    /* Semi-transparent dark bar behind text */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.70f);
    glBegin(GL_QUADS);
    glVertex2f(0, bar_y); glVertex2f(w, bar_y);
    glVertex2f(w, h);     glVertex2f(0, h);
    glEnd();
    glDisable(GL_BLEND);

    /* Line 1 – date */
    glColor3f(1.0f, 1.0f, 0.3f);
    draw_string(12.0f, bar_y + 38.0f, g_line1);

    /* Line 2 – timing */
    glColor3f(0.3f, 1.0f, 0.6f);
    draw_string(12.0f, bar_y + 12.0f, g_line2);
}

/* ---------- GLUT callbacks ------------------------------------------- */

static bool g_timing_done = false;

static void display(void)
{
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);

    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);   /* pixel coordinates, y up */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    draw_scene(w, h);
    draw_text_overlay(w, h);

    glutSwapBuffers();

    /* Capture time after the first complete frame is presented */
    if (!g_timing_done) {
        double ms = elapsed_ms();
        snprintf(g_line2, sizeof(g_line2),
                 "Test time: %.2f ms  (start -> first frame presented)", ms);
        g_timing_done = true;
        printf("%s\n", g_line2);
        glutPostRedisplay();   /* redraw to show the actual time */
    }
}

static void reshape(int w, int h)
{
    (void)w; (void)h;
    glutPostRedisplay();
}

static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q' || key == 'Q') {
        printf("\n--- Results ---\n%s\n%s\n", g_line1, g_line2);
        exit(0);
    }
}

/* ---------- main ----------------------------------------------------- */

int main(int argc, char **argv)
{
    clock_gettime(CLOCK_MONOTONIC, &g_start);

    /* Line 1: current date/time — set once at startup */
    {
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        strftime(g_line1, sizeof(g_line1),
                 "Date: %A %d %B %Y  %H:%M:%S", tm_info);
    }
    snprintf(g_line2, sizeof(g_line2), "Test time: measuring...");

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(720, 460);
    glutInitWindowPosition(200, 150);
    glutCreateWindow("OpenGL glCallLists Date Test  [Q/ESC to quit]");

    glClearColor(0.05f, 0.05f, 0.12f, 1.0f);

    build_font_lists();   /* glGenLists + 128 glNewList/glBitmap lists */

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);

    printf("%s\n", g_line1);
    glutMainLoop();
    return 0;
}
