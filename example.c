/**
 * example.c
 *
 * This code illustrates two major aspects of OpenCL/OpenGL interoperability:
 *   1) Using OpenCL to fill arrays and using OpenGL to draw them.
 *   2) Using OpenGL to create an image and using OpenCL to modify it.
 *
 * Michael Driscoll
 * mbdriscoll@gmail.com
 * April 2016
 *
 **/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <GLFW/glfw3.h>
#include <OpenCL/OpenCL.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/glext.h>

int main(void)
{
    /** GLFW initialization stuff. */
    GLFWwindow* window;
    GLsizei width  = 1024,
            height = 768;
    float ratio = width / (float) height;
    if (!glfwInit())
        exit(EXIT_FAILURE);
    window = glfwCreateWindow(width, height, "Simple example", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    /** Create a framebuffer for offscreen rendering. */
    GLuint offscreen;
    glGenFramebuffers(1, &offscreen);
    glBindFramebuffer(GL_FRAMEBUFFER, offscreen);

    /** Create a OpenGL array of vertices. */
    float _verts[] = { -0.6f, -0.4f, 0.0f, 1.0f,
                        0.6f, -0.4f, 0.0f, 1.0f,
                        0.0f,  0.6f, 0.0f, 1.0f, };
    GLuint verts;
    glGenBuffers(1, &verts);
    glBindBuffer(GL_ARRAY_BUFFER, verts);
    glBufferData(GL_ARRAY_BUFFER, sizeof(_verts), _verts, GL_DYNAMIC_DRAW);

    /** Create a OpenGL array of colors. */
    float _colors[] = { 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, };
    GLuint colors;
    glGenBuffers(1, &colors);
    glBindBuffer(GL_ARRAY_BUFFER, colors);
    glBufferData(GL_ARRAY_BUFFER, sizeof(_colors), _colors, GL_STATIC_DRAW);

    /** Get a sharegroup in preparation for initializating OpenCL context.
      * This is platform dependent. */
    CGLContextObj cgl_ctx = CGLGetCurrentContext();              
    CGLShareGroupObj cgl_sg = CGLGetShareGroup(cgl_ctx);
    cl_context_properties ctx_props[] = { 
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, (cl_context_properties) cgl_sg, 0
    };

    /** OpenCL kernel source code. */
    const char *prog_txt = "\
        kernel void twist(long frame, global float4 *out_verts, global float4 *in_verts) { \
            int i = get_global_id(0);                                                      \
            float4 v = in_verts[i];                                                        \
            float mag = length(v.xy);                                                      \
            float phi = atan2(v.y, v.x) + ((float) frame)/50.0;                            \
            out_verts[i] = (float4)(mag*cos(phi), mag*sin(phi), v.z, v.w);                 \
        }                                                                                  \
                                                                                           \
        kernel void warp(read_only image2d_t scene, write_only image2d_t screen) {         \
            int2 pos = (int2)( get_global_id(0), get_global_id(1) );                       \
            float4 val = read_imagef(scene, pos + (int2)( 15*sin(pos.y / 10.0), 0 ));      \
            write_imagef(screen, pos, val);                                                \
        }";
    const size_t prog_len = strlen(prog_txt);

    /** Instantiate OpenCL entities. */
    cl_device_id device;
    clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    cl_context ctx = clCreateContext(ctx_props, 1, &device, NULL, NULL, NULL);
    cl_command_queue queue = clCreateCommandQueue(ctx, device, 0, NULL);
    cl_program prog = clCreateProgramWithSource(ctx, 1, &prog_txt, &prog_len, NULL);
    clBuildProgram(prog, 1, &device, NULL, NULL, NULL);
    cl_kernel twist = clCreateKernel(prog, "twist", NULL);
    cl_kernel warp = clCreateKernel(prog, "warp", NULL);
    cl_mem orig_verts_cl = clCreateBuffer(ctx, CL_MEM_COPY_HOST_PTR|CL_MEM_READ_ONLY, sizeof(_verts), _verts, NULL);
    cl_mem verts_cl = clCreateFromGLBuffer(ctx, CL_MEM_READ_WRITE, verts, NULL);

    /** Create a texture to be displayed as the final image. */
    GLuint screen_tex;
    glGenTextures(1, &screen_tex);
    glBindTexture(GL_TEXTURE_2D, screen_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    cl_mem screen = clCreateFromGLTexture(ctx, CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0, screen_tex, 0);

    /** Create a texture to render the scene into. */
    GLuint scene_tex;
    glGenTextures(1, &scene_tex);
    glBindTexture(GL_TEXTURE_2D, scene_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_tex, 0);
    cl_mem scene = clCreateFromGLTexture(ctx, CL_MEM_READ_ONLY,  GL_TEXTURE_2D, 0, scene_tex,  0);

    /** Set constant kernel arguments. */
    clSetKernelArg(twist, 1, sizeof(cl_mem), &verts_cl);
    clSetKernelArg(twist, 2, sizeof(cl_mem), &orig_verts_cl);
    clSetKernelArg(warp, 0, sizeof(cl_mem), &scene);
    clSetKernelArg(warp, 1, sizeof(cl_mem), &screen);

    /** Main loop. */
    for (int frame = 0; !glfwWindowShouldClose(window); frame++)
    {
        /** Move triangle verts in 3D. */
        clEnqueueAcquireGLObjects(queue, 1, &verts_cl, 0, NULL, NULL);
        {
            size_t global = 3, local = 3;
            clSetKernelArg(twist, 0, sizeof(int),   &frame);
            clEnqueueNDRangeKernel(queue, twist, 1, NULL, &global, &local, 0, NULL, NULL);
        }
        clEnqueueReleaseGLObjects(queue, 1, &verts_cl, 0, NULL, NULL);
        clFlush(queue);

        /** Render triangle into offscreen framebuffer. */
        clEnqueueAcquireGLObjects(queue, 1, &verts_cl, 0, NULL, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, offscreen);
        {
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(-ratio, ratio, -1.f, 1.f, 1.f, -1.f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glBindBuffer(GL_ARRAY_BUFFER, verts);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(4, GL_FLOAT, 0, 0);
    
            glBindBuffer(GL_ARRAY_BUFFER, colors);
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(3, GL_FLOAT, 0, 0);

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glFlush();

        /** Use scene_tex to compute screen_tex. */
        cl_mem objs[] = { scene, screen };
        clEnqueueAcquireGLObjects(queue, 2, objs, 0, NULL, NULL);
        {
            size_t global[] = {width, height},
                    local[] = {16, 16};
            clEnqueueNDRangeKernel(queue, warp, 2, 0, global, local, 0, NULL, NULL);
        }
        clEnqueueReleaseGLObjects(queue, 2, objs, 0, NULL, NULL);
        clFlush(queue);

        /** Display screen_tex on a single quad. */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        {
            glClear( GL_COLOR_BUFFER_BIT );
            glViewport(0, 0, width, height);
            glMatrixMode( GL_PROJECTION );
            glLoadIdentity( );
            glOrtho(-ratio, ratio, -1.f, 1.f, 1.f, -1.f);
            glMatrixMode( GL_MODELVIEW );
            glLoadIdentity( );
            glEnable( GL_TEXTURE_2D );
            glBindTexture( GL_TEXTURE_2D, screen_tex);
            glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
            glBegin(GL_QUADS);
              glTexCoord2i(0, 1); glVertex2f(-ratio, 1.0f);
              glTexCoord2i(1, 1); glVertex2f( ratio, 1.0f);
              glTexCoord2i(1, 0); glVertex2f( ratio,-1.0f);
              glTexCoord2i(0, 0); glVertex2f(-ratio,-1.0f);
            glEnd();
            glDisable( GL_TEXTURE_2D );
        }
        glFlush();

        /** GLFW stuff. */
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    /** GLFW stuff. */
    glfwDestroyWindow(window);
    glfwTerminate();
}
