/*
 * glBindImageTextures name-reuse regression test.
 *
 * Covers the fix in shaderimage.c bind_image_textures(): the fast-path
 * check "texObj->Name == texture" was not guarded by DeletePending, so
 * when a texture T was deleted and a new texture Q was created with the
 * same GL name, glBindImageTextures() skipped the hash lookup and kept
 * the image unit pointing at the stale deleted T.
 *
 * The bug is most visible in multi-context scenarios (another context
 * deletes T while this context still has T cached in u->TexObj), but the
 * format-check test below also catches it in single-context when name
 * reuse occurs before the image unit is explicitly cleared.
 *
 * Tests:
 *   1. image unit cleared on glDeleteTextures (single-context GL spec
 *      requirement: GL_IMAGE_BINDING_NAME must become 0).
 *   2. glBindImageTextures rebinds to Q (not stale T) after name reuse:
 *      T uses GL_RGBA8, Q uses GL_R32F; after delete+create+rebind,
 *      GL_IMAGE_BINDING_FORMAT must be GL_R32F.
 *   3. Data correctness via compute shader (GL 4.3+): write distinct
 *      values into T and Q; after rebind, a compute shader reading image
 *      unit 0 must see Q's value, not T's.
 *
 * Requires: GL 4.2 (image units, glTexStorage2D) for tests 1-2.
 *           GL 4.3 (compute shaders) for test 3.
 *           GL 4.4 or GL_ARB_multi_bind for glBindImageTextures (tests 2-3).
 *
 * Build (Ubuntu):
 *   gcc -O2 -Wall -o gl_bind_image_textures_name_reuse_test \
 *       gl_bind_image_textures_name_reuse_test.c -lGL -lglut -lm
 *   sudo apt-get install freeglut3-dev libgl-dev   # if needed
 *
 * Run:
 *   ./gl_bind_image_textures_name_reuse_test
 *   Exit code 0 = all tests passed, 1 = one or more failures.
 */

#include <GL/gl.h>
#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---------- helpers -------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void check(const char *label, bool ok)
{
    printf("  %s: %s\n", ok ? "PASS" : "FAIL", label);
    if (ok) g_pass++;
    else    g_fail++;
}

static void skip(const char *reason)
{
    printf("  SKIP: %s\n", reason);
    g_skip++;
}

static bool gl_version_at_least(int major, int minor)
{
    GLint maj = 0, min = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &min);
    return (maj > major) || (maj == major && min >= minor);
}

static bool have_multi_bind(void)
{
    return gl_version_at_least(4, 4) ||
           glutExtensionSupported("GL_ARB_multi_bind");
}

/* ---------- test 1: image unit cleared on texture delete ------------------- */

static void test_image_unit_cleared_on_delete(void)
{
    printf("\n--- Test 1: image unit cleared when texture is deleted ---\n");

    if (!gl_version_at_least(4, 2)) {
        skip("requires GL 4.2 (image units + glTexStorage2D)");
        return;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);

    GLint bound;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &bound);
    check("image unit 0 bound to tex before delete", (GLuint)bound == tex);

    /* GL spec: deleting a texture bound to an image unit clears that unit. */
    glDeleteTextures(1, &tex);

    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &bound);
    check("image unit 0 cleared (name == 0) after delete", bound == 0);
}

/* ---------- test 2: rebind binds Q not stale T (format check) -------------- */

/*
 * Sequence:
 *   T (GL_RGBA8) -> bound to image unit 0 -> deleted -> name N freed
 *   Q (GL_R32F)  -> created, gets name N (reuse) -> glBindImageTextures(N)
 *   GL_IMAGE_BINDING_FORMAT must be GL_R32F (Q), not GL_RGBA8 (stale T).
 *
 * Without the DeletePending fix, the fast-path sees T->Name == N and
 * skips the lookup, keeping the stale T in the image unit.
 */
static void test_rebind_after_name_reuse_format(void)
{
    printf("\n--- Test 2: glBindImageTextures rebinds Q (not stale T) "
           "after name reuse [format check] ---\n");

    if (!gl_version_at_least(4, 2)) {
        skip("requires GL 4.2 (image units + glTexStorage2D)");
        return;
    }
    if (!have_multi_bind()) {
        skip("requires GL 4.4 or GL_ARB_multi_bind (glBindImageTextures)");
        return;
    }

    /* Create T with GL_RGBA8 and bind it to image unit 0. */
    GLuint name_T;
    glGenTextures(1, &name_T);
    glBindTexture(GL_TEXTURE_2D, name_T);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindImageTextures(0, 1, &name_T);

    GLint fmt;
    glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0, &fmt);
    check("image unit 0 initially GL_RGBA8 (T)", fmt == GL_RGBA8);

    /* Delete T: name_T returned to the pool. */
    glDeleteTextures(1, &name_T);

    /* Create Q: must reuse name_T for the test to be meaningful. */
    GLuint name_Q;
    glGenTextures(1, &name_Q);

    if (name_Q != name_T) {
        glDeleteTextures(1, &name_Q);
        skip("name not reused (implementation does not reuse names); "
             "test requires name_Q == name_T");
        return;
    }

    /* Give Q a distinct internal format so we can tell it apart from T. */
    glBindTexture(GL_TEXTURE_2D, name_Q);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, 1, 1);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Critical call: rebind using the reused name.
     * Must update the image unit to Q (GL_R32F). */
    glBindImageTextures(0, 1, &name_Q);

    glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0, &fmt);
    check("image unit 0 is GL_R32F (Q) after rebind, not stale GL_RGBA8 (T)",
          fmt == GL_R32F);

    /* Cleanup. */
    glDeleteTextures(1, &name_Q);
    GLuint zero = 0;
    glBindImageTextures(0, 1, &zero);
}

/* ---------- test 3: data correctness via compute shader -------------------- */

static const char *compute_src =
    "#version 430\n"
    "layout(local_size_x = 1) in;\n"
    "layout(r32i, binding = 0) uniform iimage2D img;\n"
    "layout(std430, binding = 0) buffer Result { int value; };\n"
    "void main() { value = imageLoad(img, ivec2(0,0)).r; }\n";

static GLuint build_compute(void)
{
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &compute_src, NULL);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "compute shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader);
    glLinkProgram(prog);
    glDeleteShader(shader);

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "compute shader link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

/*
 * T stores value 111, Q stores value 999 (both GL_R32I, 1x1).
 * After T is deleted and Q is created with the same name, glBindImageTextures
 * is called with that name.  The compute shader reads image unit 0 and
 * writes the result to an SSBO.  We expect 999 (Q), not 111 (stale T).
 */
static void test_rebind_after_name_reuse_data(void)
{
    printf("\n--- Test 3: glBindImageTextures rebinds Q (not stale T) "
           "after name reuse [compute shader data check] ---\n");

    if (!gl_version_at_least(4, 3)) {
        skip("requires GL 4.3 (compute shaders)");
        return;
    }
    if (!have_multi_bind()) {
        skip("requires GL 4.4 or GL_ARB_multi_bind (glBindImageTextures)");
        return;
    }

    GLuint prog = build_compute();
    if (!prog) {
        skip("compute shader compilation failed");
        return;
    }

    /* Result SSBO. */
    GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    int init = 0;
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int), &init, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

    /* Create T (value 111). */
    GLuint name_T;
    glGenTextures(1, &name_T);
    glBindTexture(GL_TEXTURE_2D, name_T);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32I, 1, 1);
    int val_T = 111;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1,
                    GL_RED_INTEGER, GL_INT, &val_T);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Bind T to image unit 0 and verify the compute shader reads 111. */
    glBindImageTextures(0, 1, &name_T);
    glUseProgram(prog);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    int result = 0;
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(int), &result);
    check("compute reads 111 from T before delete", result == 111);

    /* Delete T: name_T freed. */
    glDeleteTextures(1, &name_T);

    /* Create Q (value 999): must reuse name_T. */
    GLuint name_Q;
    glGenTextures(1, &name_Q);

    if (name_Q != name_T) {
        glDeleteTextures(1, &name_Q);
        glDeleteProgram(prog);
        glDeleteBuffers(1, &ssbo);
        skip("name not reused; test requires name_Q == name_T");
        return;
    }

    glBindTexture(GL_TEXTURE_2D, name_Q);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32I, 1, 1);
    int val_Q = 999;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1,
                    GL_RED_INTEGER, GL_INT, &val_Q);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Rebind to image unit 0 using the reused name. */
    glBindImageTextures(0, 1, &name_Q);

    /* Reset SSBO and dispatch. */
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(int), &init);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(int), &result);
    check("compute reads 999 from Q after rebind (not stale 111 from T)",
          result == 999);

    /* Cleanup. */
    glDeleteTextures(1, &name_Q);
    glDeleteProgram(prog);
    glDeleteBuffers(1, &ssbo);
    GLuint zero = 0;
    glBindImageTextures(0, 1, &zero);
}

/* ---------- GLUT callbacks ------------------------------------------------- */

static void display(void)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    test_image_unit_cleared_on_delete();
    test_rebind_after_name_reuse_format();
    test_rebind_after_name_reuse_data();

    printf("\n=== Results: %d passed, %d failed, %d skipped ===\n",
           g_pass, g_fail, g_skip);

    glutSwapBuffers();
    exit(g_fail > 0 ? 1 : 0);
}

static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q' || key == 'Q')
        exit(g_fail > 0 ? 1 : 0);
}

/* ---------- main ----------------------------------------------------------- */

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    /* Core profile needed for compute shaders and image units. */
    glutInitContextVersion(4, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(64, 64);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("glBindImageTextures name-reuse test");

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);

    glutMainLoop();
    return 1; /* unreachable */
}
